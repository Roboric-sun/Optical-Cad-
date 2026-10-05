"""Validate a deployed .app without relying on a developer's Qt installation."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import os
import zipfile
import plistlib


def verify(bundle: Path) -> None:
    bundle = bundle.resolve(strict=True)
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(bundle)], check=True)
    magic = {bytes.fromhex(v) for v in (
        "feedface", "cefaedfe", "feedfacf", "cffaedfe", "cafebabe", "bebafeca",
        "cafebabf", "bfbafeca",
    )}
    dependency_commands = {
        "LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
        "LC_LAZY_LOAD_DYLIB", "LC_LOAD_UPWARD_DYLIB",
    }
    objects = 0
    for path in bundle.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) not in magic:
                continue
        objects += 1
        commands = subprocess.check_output(["otool", "-l", str(path)], text=True)
        command = ""
        for row in commands.splitlines():
            row = row.strip()
            if row.startswith("cmd "):
                command = row[4:]
            # LC_ID_DYLIB names the library itself; it is not an imported dependency.
            is_dependency = command in dependency_commands and row.startswith("name ")
            is_search_path = command == "LC_RPATH" and row.startswith("path ")
            if (is_dependency or is_search_path) and (
                "/opt/homebrew/" in row or "/usr/local/" in row
            ):
                raise RuntimeError(f"External dependency in {path}: {command}: {row}")
    if not objects:
        raise RuntimeError("No Mach-O executables in package")
    environment = {
        key: value for key, value in os.environ.items()
        if not key.startswith(("QT_", "DYLD_", "QML"))
    }
    environment["QT_QPA_PLATFORM"] = "offscreen"
    with tempfile.TemporaryDirectory(prefix="optical-cad-package-") as output:
        subprocess.run(
            [str(bundle / "Contents/MacOS/optical_cad"), "--write-examples", output],
            env=environment, check=True, timeout=60,
        )
        batch = bundle / "Contents/MacOS/optics_batch"
        request = {"operation": "analyze", "project": json.loads((Path(output) / "singlet.optcad").read_text())}
        completed = subprocess.run([str(batch)], input=json.dumps(request), text=True,
                                   capture_output=True, env=environment, check=True, timeout=60)
        if not json.loads(completed.stdout).get("ok"):
            raise RuntimeError("Packaged batch API failed")
        sdk = bundle / "Contents/Resources/python/opticalcad.py"
        if not sdk.is_file():
            raise RuntimeError("Python SDK is missing from the application bundle")
        for name in ("singlet", "achromat", "led_illuminator", "spectral_prism", "linked_singlet", "marginal_focus", "vignetted_singlet", "object_height", "image_height", "real_image_height", "aspheric_singlet"):
            project = json.loads((Path(output) / f"{name}.optcad").read_text())
            if project["format"] != "optical-cad" or not project["sequential"]["surfaces"]:
                raise RuntimeError(f"Invalid generated example: {name}")
            if name == "spectral_prism" and (
                project["nonsequential"]["objects"][0]["kind"] != 6
                or len(project["nonsequential"]["sources"]) != 3
            ):
                raise RuntimeError("Spectral prism example is incomplete")
            if name == "linked_singlet" and (
                project["version"] != 2 or len(project["sequential"].get("solves", [])) != 2
            ):
                raise RuntimeError("Constrained lens example is incomplete")
            if name == "marginal_focus" and (
                project["version"] != 4 or len(project["sequential"].get("solves", [])) != 1
                or project["sequential"]["solves"][0]["kind"] != 4
            ):
                raise RuntimeError("Ray-height example is incomplete")
            if name == "vignetted_singlet" and (
                project["version"] != 5 or len(project["sequential"]["fields"][1]) != 7
                or project["sequential"]["fields"][1][5:] != [.2, .25]
            ):
                raise RuntimeError("Vignetting example is incomplete")
            if name in ("object_height", "image_height") and (
                project["version"] != 6
                or project["sequential"]["fieldType"] != (1 if name == "object_height" else 2)
            ):
                raise RuntimeError("Height-field example is incomplete")
            if name == "real_image_height" and (
                project["version"] != 7 or project["sequential"]["fieldType"] != 3
            ):
                raise RuntimeError("Real image height example is incomplete")
            if name == "aspheric_singlet" and (
                project["version"] != 7
                or len(project["sequential"]["surfaces"][0]["asphere"]) != 10
                or project["sequential"]["surfaces"][1]["oddAsphere"][0] == 0
                or not project["workspace"].get("polychromaticDiffraction")
            ):
                raise RuntimeError("Extended aspheric example is incomplete")
    # Validate companion files in the full ZIP too, not just the executable bundle.
    with (bundle / "Contents/Info.plist").open("rb") as plist:
        metadata = plistlib.load(plist)
        version = metadata.get("CFBundleShortVersionString", metadata["CFBundleVersion"])
    archive = bundle.parent / f"OpticalCAD-{version}-macOS-arm64.zip"
    if archive.exists():
        with zipfile.ZipFile(archive) as zipped:
            names = zipped.namelist()
            prefix = names[0].split("/")[0] + "/"
            required = ["docs/ROADMAP_RU.md", "docs/BENCHMARKS_RU.md",
                        "scripts/reference_doublet.py", "examples/marginal_focus.optcad",
                        "examples/geopter/kingslake_doublet.json", "examples/geopter/dbgauss.json",
                        "examples/vignetted_singlet.optcad", "examples/object_height.optcad",
                        "examples/image_height.optcad", "docs/FIELDS_RU.md", "scripts/opticalcad.py",
                        "docs/RELEASE_1_0_RU.md", "docs/PYTHON_API_RU.md",
                        "docs/benchmarks/geopter-1.0-matched.json",
                        "examples/real_image_height.optcad", "examples/aspheric_singlet.optcad",
                        "optical_cad.app/Contents/Resources/python/opticalcad.py",
                        "optical_cad.app/Contents/MacOS/optics_batch"]
            for relative in required:
                if prefix + relative not in names:
                    raise RuntimeError(f"Missing companion file in {archive}: {relative}")
    print(f"Package OK: {objects} Mach-O objects, local dependencies, eleven generated examples, batch API and Python SDK.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 scripts/verify_macos_package.py /path/to/optical_cad.app")
    verify(Path(sys.argv[1]))
