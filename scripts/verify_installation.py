"""Accept the deployed app from a fresh directory with developer paths removed.

Usage: python verify_installation.py /path/to/install /path/to/report.json
Works on the macOS ZIP and the Windows portable installation directory.
"""
import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

source, report = (Path(value).resolve() for value in sys.argv[1:3])
with tempfile.TemporaryDirectory(prefix="opticalcad-installation-") as temporary:
    base = Path(temporary)
    if sys.platform == "darwin":
        archives = sorted(source.glob("OpticalCAD-*-macOS-arm64.zip"), key=lambda p: p.stat().st_mtime)
        if not archives:
            raise RuntimeError("Full macOS release ZIP missing")
        subprocess.run(["ditto", "-x", "-k", str(archives[-1]), str(base)], check=True)
        package = next(base.glob("OpticalCAD-*"))
        executable = package / "optical_cad.app/Contents/MacOS/optical_cad"
        batch = executable.with_name("optics_batch")
        subprocess.run([sys.executable, str(Path(__file__).with_name("verify_macos_package.py")),
                        str(package / "optical_cad.app")], check=True)
    else:
        archives = sorted(source.glob("OpticalCAD-*-Windows-x64.zip"), key=lambda p: p.stat().st_mtime)
        if not archives: raise RuntimeError("Full Windows portable ZIP missing")
        shutil.unpack_archive(archives[-1], base)
        package = next(base.glob("OpticalCAD-*"))
        executable = package / "bin/optical_cad.exe"
        batch = package / "bin/optics_batch.exe"
        config = configparser.ConfigParser()
        config.read(executable.with_name("qt.conf"), encoding="utf-8")
        prefix = executable.parent / config.get("Paths", "Prefix", fallback=".")
        plugins = prefix / config.get("Paths", "Plugins", fallback="plugins")
        for name in ("qwindows.dll", "qoffscreen.dll"):
            if not (plugins / "platforms" / name).is_file():
                raise RuntimeError(f"Windows archive missing platform plugin: {name}")
        for name in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"):
            if not executable.with_name(name).is_file():
                raise RuntimeError(f"Windows archive missing runtime: {name}")
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("QT_", "DYLD_", "QML", "OPTICALCAD_", "PYTHONPATH"))}
    environment["QT_QPA_PLATFORM"] = "offscreen"
    environment["OPTICALCAD_PYTHON"] = sys.executable
    (base / "home").mkdir()
    environment["XDG_CONFIG_HOME"] = str(base / "home/config")
    environment["XDG_CACHE_HOME"] = str(base / "home/cache")
    if sys.platform == "win32":
        system = Path(os.environ["SystemRoot"])
        environment["PATH"] = str(system / "System32") + os.pathsep + str(system)
        environment["QT_QPA_FONTDIR"] = str(system / "Fonts")
        environment["USERPROFILE"] = str(base / "home")
        environment["APPDATA"] = str(base / "home/appdata")
        environment["LOCALAPPDATA"] = str(base / "home/localappdata")
    else:
        environment["PATH"] = "/usr/bin:/bin"
    gui_report = base / "gui-acceptance.json"
    qt_root = None
    # Only hide the SDK installed by this ephemeral GitHub Actions job.
    # QT_ROOT_DIR is the documented output of jurplel/install-qt-action.
    if os.environ.get("GITHUB_ACTIONS") == "true":
        qt_root = Path(os.environ["QT_ROOT_DIR"]).resolve()
        hidden = qt_root.with_name(qt_root.name + "-acceptance-hidden")
        qt_root.rename(hidden)
    result = {"ok": False, "platform": sys.platform, "qt_sdk_hidden": qt_root is not None}
    try:
        try:
            completed = subprocess.run([str(executable), "--verify-installation", str(gui_report)],
                                       env=environment, cwd=base, capture_output=True, text=True, timeout=150)
            result.update(json.loads(gui_report.read_text(encoding="utf-8")) if gui_report.exists() else {
                "ok": False, "error": "Deployed GUI did not write its report"})
            result.update(returncode=completed.returncode, output=completed.stdout + completed.stderr)
            if completed.returncode or not result.get("ok"):
                raise RuntimeError("Deployed GUI acceptance failed")
            # Keep the development SDK hidden for the batch executable as well.
            sys.path.insert(0, str(package / "scripts"))
            from opticalcad import Project
            examples = sorted((package / "examples").glob("*.optcad"))
            if len(examples) != 11:
                raise RuntimeError(f"Expected 11 examples, found {len(examples)}")
            for path in examples:
                project = Project.load(path, executable=batch)
                if not project.validate():
                    raise RuntimeError(f"Invalid packaged example: {path.name}")
            result["examples_validated"] = len(examples)
            project = Project.load(package / "examples/aspheric_singlet.optcad", executable=batch)
            if abs(sum(project.diffraction(size=32)["psf"]) - 1) >= 1e-10:
                raise RuntimeError("Packaged diffraction is not normalized")
            result["batch_diffraction"] = True
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""
            result.update(ok=False, returncode=-1, error="Deployed GUI timed out after 150 seconds",
                          output=decoded(error.stdout) + decoded(error.stderr))
        except Exception as error:
            result.update(ok=False, error=str(error))
    finally:
        if qt_root is not None:
            hidden.rename(qt_root)
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not result.get("ok"):
        raise RuntimeError(result)
    print(f"Installation accepted on {sys.platform}: GUI editing, undo/redo, save/reopen, Python and descendant cancellation; 11 examples and spectral diffraction")
