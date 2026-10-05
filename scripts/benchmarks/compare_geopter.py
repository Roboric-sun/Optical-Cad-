"""Compare independent engines using identical launch rays and primary-wave indices.

Usage: python compare_geopter.py /path/to/geopter_probe /path/to/optics_batch output_dir
The probe must be compiled against Geopter 0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7.
Native default pupil sampling is deliberately also recorded, not claimed equivalent.
"""
import json
import math
from pathlib import Path
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from opticalcad import Project
probe, binary, target = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3])
target.mkdir(parents=True, exist_ok=True)
examples = [ROOT / "tests/data/geopter_singlet.json"] + [ROOT / "examples/geopter" / (name + ".json") for name in
            ("kingslake_doublet", "sasian_triplet_glass", "dbgauss", "aspheric_singlet")]
summary = []
for source in examples:
    p = Project.import_geopter(source, executable=binary)
    report = p.analyze(pupil_grid=9)
    rays = p._request("trace_rays", pupil_grid=9)
    launch = target / (source.stem + "-launch.json")
    launch.write_text(json.dumps(rays), encoding="utf-8")
    geometry = p._request("export_geopter")["data"]
    geometry.pop("OpticalCADExtension")
    # Freeze the exact index supplied to both tracers at the primary wavelength.
    # This isolates geometry from different editions of SCHOTT and Cauchy/Buchdahl.
    agf = []
    for i, surface in enumerate(p.system["surfaces"], 1):
        index = report["refractive_indices"][surface["material"]]
        if surface["kind"] == 2:
            material = "AIR" if i == 1 else geometry["Assembly"][str(i-1)]["Material"]
        elif surface["material"] == "AIR":
            material = "AIR"
        else:
            name = "MATCH" + str(i)
            material = name + "_SCHOTT"
            agf.append(f"NM {name} 2 0 {index:.17g} 60\nCD {index*index-1:.17g} 0 0 0 0 0\nTD 0 0 0 0 0 0 25\nLD 0.2 5\n")
        geometry["Assembly"][str(i)]["Material"] = material
    catalog = target / "SCHOTT.agf"
    catalog.write_text("".join(agf), encoding="utf-8")
    data = target / (source.stem + "-matched.json")
    data.write_text(json.dumps(geometry), encoding="utf-8")
    reference_path = target / (source.stem + "-reference.json")
    process = subprocess.run([str(probe), str(data), str(reference_path), str(catalog), str(launch)],
                             capture_output=True, text=True, timeout=60)
    (target / (source.stem + "-probe.log")).write_text(process.stdout + process.stderr)
    if process.returncode:
        raise RuntimeError(f"Geopter failed for {source.name}: {process.returncode}")
    ref = json.loads(reference_path.read_text())
    point_error = opl_error = 0
    complete = 0
    for our, their in zip(rays["rays"], ref["matched_rays"]):
        assert our["complete"] and their["status"] == 0, (source.name, "incomplete ray")
        assert len(our["points"]) == len(their["points"])
        complete += 1
        point_error = max(point_error, max(abs(x-y) for a,b in zip(our["points"],their["points"]) for x,y in zip(a,b)))
        opl_error = max(opl_error, abs(our["opl_mm"]-their["opl_mm"]))
    row = dict(system=source.stem, rays=complete, efl_error_mm=abs(report["efl_mm"]-ref["efl_mm"]),
               bfl_error_mm=abs(report["bfl_mm"]-ref["bfl_mm"]), max_intersection_error_mm=point_error,
               max_opl_error_mm=opl_error)
    row["passed"] = row["efl_error_mm"] < 1e-8 and row["bfl_error_mm"] < 1e-8 and point_error < 1e-7 and opl_error < 1e-7
    summary.append(row)
    print(json.dumps(row))
(target / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
if not all(row["passed"] for row in summary):
    raise SystemExit(1)
