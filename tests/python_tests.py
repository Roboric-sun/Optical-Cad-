"""Integration through a real batch subprocess and the shipped Python client."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(sys.argv[2]) / "scripts"))
from opticalcad import Project, OpticalCADError
binary, root = sys.argv[1], Path(sys.argv[2])
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    p = Project.load(root / "examples/singlet.optcad", executable=binary)
    first = p.analyze(pupil_grid=9)
    assert first["efl_mm"] > 0 and first["fields"][0]["survived"] > 0
    p.report(tmp / "first.json", pupil_grid=9)
    p.report(tmp / "second.json", pupil_grid=9)
    assert (tmp / "first.json").read_bytes() == (tmp / "second.json").read_bytes()
    old_radius = p.system["surfaces"][0]["radius"]
    p.system["surfaces"][0]["radius"] = old_radius * 1.1
    p.autofocus()
    assert p.analyze()["efl_mm"] != first["efl_mm"]
    p.save(tmp / "changed.optcad")
    assert Project.load(tmp / "changed.optcad", executable=binary).system["surfaces"][0]["radius"] == old_radius * 1.1
    p.export_geopter(tmp / "geopter.json")
    imported = Project.import_geopter(tmp / "geopter.json", executable=binary)
    assert imported.data == p.data
    p.system["pupilDiameter"] = -1
    before = (tmp / "changed.optcad").read_bytes()
    try:
        p.save(tmp / "changed.optcad")
        raise AssertionError("Invalid save should fail")
    except OpticalCADError:
        pass
    assert (tmp / "changed.optcad").read_bytes() == before
    for data in ("{", json.dumps({"operation":"unknown"}), json.dumps({"operation":"validate","project":{}})):
        process = subprocess.run([binary], input=data, text=True, capture_output=True)
        assert process.returncode == 1 and not json.loads(process.stdout)["ok"]
    good = Project.load(root / "examples/singlet.optcad", executable=binary)
    good.system["pupilDiameter"] = .5
    good.autofocus()
    d = good.diffraction(size=32)
    assert len(d["psf"]) == 1024 and abs(sum(d["psf"]) - 1) < 1e-10
    wf = good.wavefront(pupil_grid=9)
    assert len(wf["samples"]) == 49 and wf["rms_mm"] >= 0
    for coordinates in ([], [["wrong", 0]], [[3, 0]]):
        try:
            good._request("trace_rays", pupils=coordinates)
            raise AssertionError("Invalid pupil coordinates should fail")
        except OpticalCADError:
            pass
    scene = good.trace_scene()
    assert abs(scene["launched_w"]-sum(scene[k] for k in ("detected_w","absorbed_w","escaped_w","truncated_w"))) < 1e-9
print("Python integration: load, edit, autofocus, reports, diffraction, scene, export/import and failures passed")
