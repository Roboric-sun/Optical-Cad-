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
        for name in ("singlet", "achromat", "led_illuminator", "spectral_prism", "linked_singlet", "marginal_focus", "vignetted_singlet"):
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
    # Validate companion files in the full ZIP too, not just the executable bundle.
    with (bundle / "Contents/Info.plist").open("rb") as plist:
        version = plistlib.load(plist)["CFBundleVersion"]
    archive = bundle.parent / f"OpticalCAD-{version}-macOS-arm64.zip"
    if archive.exists():
        with zipfile.ZipFile(archive) as zipped:
            names = zipped.namelist()
            prefix = names[0].split("/")[0] + "/"
            required = ["docs/ROADMAP_RU.md", "docs/BENCHMARKS_RU.md",
                        "scripts/reference_doublet.py", "examples/marginal_focus.optcad",
                        "examples/geopter/kingslake_doublet.json", "examples/geopter/dbgauss.json",
                        "examples/vignetted_singlet.optcad", "docs/FIELDS_RU.md"]
            for relative in required:
                if prefix + relative not in names:
                    raise RuntimeError(f"Missing companion file in {archive}: {relative}")
    print(f"Package OK: {objects} Mach-O objects, local dependencies, seven generated examples.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python3 scripts/verify_macos_package.py /path/to/optical_cad.app")
    verify(Path(sys.argv[1]))
