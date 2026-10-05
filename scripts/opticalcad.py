"""Optical CAD Python API. Millimetres, micrometres, watts; no third-party modules.

Set OPTICALCAD_BATCH to the optics_batch executable, or pass executable=.
The same C++ project loader and calculations are used by the desktop application.
"""
from __future__ import annotations
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

class OpticalCADError(RuntimeError):
    pass

def _executable():
    configured = os.environ.get("OPTICALCAD_BATCH")
    if configured:
        return configured
    root = Path(__file__).resolve().parent.parent
    for candidate in (root / "optical_cad.app/Contents/MacOS/optics_batch",
                      root / "bin/optics_batch.exe", root / "bin/optics_batch",
                      root / "build/optics_batch", root / "build/Release/optics_batch.exe"):
        if candidate.is_file():
            return str(candidate)
    found = shutil.which("optics_batch")
    if found:
        return found
    raise OpticalCADError("Set OPTICALCAD_BATCH to the optics_batch executable")

class Project:
    def __init__(self, data, *, executable=None, timeout=120):
        self.data = copy.deepcopy(data)
        self.executable = str(executable or _executable())
        self.timeout = timeout

    @classmethod
    def load(cls, path, **kwargs):
        result = cls(json.loads(Path(path).read_text(encoding="utf-8")), **kwargs)
        result.validate()
        return result

    @classmethod
    def import_geopter(cls, path, **kwargs):
        project = cls({}, **kwargs)
        project.data = project._request("import_geopter", data=json.loads(Path(path).read_text(encoding="utf-8")))["project"]
        return project

    @property
    def system(self):
        return self.data["sequential"]

    @property
    def scene(self):
        return self.data["nonsequential"]

    def _request(self, operation, **options):
        request = {"operation": operation, "project": self.data, "options": options}
        if operation == "import_geopter":
            request["data"] = options["data"]
        process = subprocess.run([self.executable], input=json.dumps(request, allow_nan=False),
                                 text=True, encoding="utf-8", capture_output=True, timeout=self.timeout)
        try:
            response = json.loads(process.stdout)
        except ValueError as error:
            raise OpticalCADError(process.stderr or "Batch process returned invalid JSON") from error
        if process.returncode or not response.get("ok"):
            raise OpticalCADError(response.get("error", process.stderr))
        result = response["result"]
        if "project" in result:
            self.data = result["project"]
        return result

    def validate(self):
        return self._request("validate")["valid"]

    def analyze(self, *, pupil_grid=17):
        return self._request("analyze", pupil_grid=pupil_grid)

    def diffraction(self, *, field=0, size=64, pupil_grid=None):
        return self._request("diffraction", field=field, size=size, pupil_grid=pupil_grid or size // 2 + 1)

    def autofocus(self):
        return self._request("autofocus")["image_z_mm"]

    def optimize(self):
        return self._request("optimize")

    def trace_scene(self):
        return self._request("trace_scene")

    def save(self, path):
        self.validate()
        _atomic_json(path, self.data)

    def export_geopter(self, path):
        _atomic_json(path, self._request("export_geopter")["data"])

    def report(self, path, *, pupil_grid=17):
        """Write a deterministic JSON calculation report; no wall-clock metadata."""
        result = self.analyze(pupil_grid=pupil_grid)
        _atomic_json(path, result)
        return result

def _atomic_json(path, data):
    path = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=path.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = stream.name
            json.dump(data, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)
