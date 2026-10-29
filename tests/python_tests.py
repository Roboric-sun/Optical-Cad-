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
    dls=Project.load(root / "examples/dls_focus.optcad",executable=binary)
    optimized=dls.optimize()
    assert optimized["algorithm"]==1 and len(optimized["starts"])==3 and optimized["after"]<optimized["before"]
    assert all(b<=a for a,b in zip(optimized["history"],optimized["history"][1:]))
    dls.save(tmp / "dls.optcad")
    assert Project.load(tmp / "dls.optcad",executable=binary).data==dls.data
    common=Project.load(root / "examples/singlet.optcad",executable=binary)
    assert common.autofocus(all_fields=True,pupil_grid=9)>0
    for name in ("folded_mirror", "coordinate_group", "afocal_telescope"):
        model=Project.load(root / f"examples/{name}.optcad",executable=binary)
        report=model.analyze(pupil_grid=9)
        assert report["fields"][0]["survived"]>0
        matrix=model.first_order()
        assert len(matrix["matrix"])==4
        if name=="afocal_telescope":assert matrix["afocal"] and abs(matrix["matrix"][2][2]+2)<1e-5
        model.save(tmp / f"{name}.optcad")
        assert Project.load(tmp / f"{name}.optcad",executable=binary).data==model.data
    for name in ("cylindrical_lens","toroidal_lens","obscured_aperture"):
        model=Project.load(root/f"examples/{name}.optcad",executable=binary)
        assert model.data["version"]==10 and model.analyze(pupil_grid=9)["fields"][0]["survived"]>0
        assert len(model.first_order()["matrix"])==4
        model.save(tmp/f"{name}.optcad")
        assert Project.load(tmp/f"{name}.optcad",executable=binary).data==model.data
print("Python integration: new surface profiles and apertures, local coordinates, first order, DLS, common focus, load, edit, autofocus, reports, diffraction, scene, export/import and failures passed")

# Configurations, statistical tolerances, and independently saved manufactured optics.
with tempfile.TemporaryDirectory() as location:
    location=Path(location)
    design=Project.load(root/"examples/conjugates_tolerances.optcad",executable=binary)
    before=design.analyze_configurations()
    assert len(before["states"])==3 and all(s["analysis"]["fields"][0]["survived"] for s in before["states"])
    joint=design.optimize_configurations()
    assert joint["after"]<=joint["before"] and joint["algorithm"]=="bounded coordinate"
    design.data["tolerance"]["samples"]=8
    design.data["tolerance"]["compensationIterations"]=4
    report=design.tolerances()
    assert report["after"]["total"]==8 and report["after"]["lower95"]<=report["after"]["fraction"]<=report["after"]["upper95"]
    assert report==design.tolerances()
    specimen=design.tolerance_sample(report["samples"][0],state=1)
    specimen.save(location/"specimen.optcad")
    assert Project.load(location/"specimen.optcad",executable=binary).data==specimen.data
    assert "toleranceRealization" in specimen.data["workspace"] and "configurations" not in specimen.data
    free=Project.load(root/"examples/xy_freeform.optcad",executable=binary)
    assert free.analyze(pupil_grid=9)["fields"][0]["survived"] and len(free.first_order()["matrix"])==4
    free.save(location/"free.optcad")
    assert Project.load(location/"free.optcad",executable=binary).data==free.data
print("Configuration, tolerance and XY freeform Python integration passed")
