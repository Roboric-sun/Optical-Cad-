"""Independent Geopter rays + NumPy dense Fourier quadrature acceptance.

Usage: python verify_diffraction.py geopter_probe optics_batch output_directory
Requires NumPy only for this external benchmark, not for the application SDK.
Reference geometry, refraction and OPL come from the pinned external engine.
Sphere roots use NumPy polynomial roots; Fourier sums use dense matrix algebra;
MTF uses NumPy FFT. No optical calculation is imported from our C++ core.
"""
import copy
import json
from pathlib import Path
import subprocess
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from opticalcad import Project
probe, binary, target = (Path(value).resolve() for value in sys.argv[1:4])
target.mkdir(parents=True, exist_ok=True)

def external(project, pupils, name):
    launch = project._request("trace_rays", pupils=pupils, field=0)
    data = project._request("export_geopter")["data"]
    data.pop("OpticalCADExtension")
    # Nonzero auxiliary field prevents undefined on-axis first-order pupil data.
    if max(abs(v) for v in data["Spec"]["Field"]["Y"]) == 0:
        field = data["Spec"]["Field"]
        for key, value in (("X", 0), ("Y", 1), ("Weight", 1), ("VUX", 0), ("VLX", 0), ("VUY", 0), ("VLY", 0)):
            if key in field: field[key].append(value)
        if "Color" in field: field["Color"].append([0, 0, 1, 1])
    indices = project.analyze(pupil_grid=3)["refractive_indices"]
    glass = []
    for i, surface in enumerate(project.system["surfaces"], 1):
        index = indices[surface["material"]]
        material = "AIR"
        if surface["kind"] == 2:
            material = "AIR" if i == 1 else data["Assembly"][str(i-1)]["Material"]
        elif surface["material"] != "AIR":
            material = f"MATCH{i}_SCHOTT"
            glass.append(f"NM MATCH{i} 2 0 {index:.17g} 60\nCD {index*index-1:.17g} 0 0 0 0 0\nTD 0 0 0 0 0 0 25\nLD .2 5\n")
        data["Assembly"][str(i)]["Material"] = material
    data["Assembly"][str(len(project.system["surfaces"])+1)]["Material"] = material
    prefix = target / name
    catalog = target / "SCHOTT.agf"; catalog.write_text("".join(glass))
    geometry = prefix.with_suffix(".json"); geometry.write_text(json.dumps(data))
    rays = prefix.with_suffix(".launch.json"); rays.write_text(json.dumps(launch))
    reference = prefix.with_suffix(".reference.json")
    completed = subprocess.run([str(probe), str(geometry), str(reference), str(catalog), str(rays)],
                               capture_output=True, text=True, timeout=120)
    prefix.with_suffix(".log").write_text(completed.stdout + completed.stderr)
    if completed.returncode: raise RuntimeError(name)
    result = json.loads(reference.read_text())
    assert len(launch["rays"]) == len(result["matched_rays"])
    assert all(our["complete"] and ref["status"] == 0 for our, ref in zip(launch["rays"], result["matched_rays"])), name
    vertices = np.cumsum([0] + [s["thickness"] for s in project.system["surfaces"][:-1]])
    points = np.asarray([row["points"] for row in result["matched_rays"]])
    points[:, :-1, 2] += vertices
    points[:, -1, 2] += project.system["surfaces"][-1]["thickness"] + vertices[-1] + project.system["defocus"]
    direction = np.asarray([row["exit_direction"] for row in result["matched_rays"]])
    opl = np.asarray([row["opl_mm"] for row in result["matched_rays"]])
    # Derive Fresnel transmission from external segment directions and Snell.
    power = np.ones(len(points)); before = np.asarray([row["direction"] for row in launch["rays"]]); n = 1.
    for i, surface in enumerate(project.system["surfaces"]):
        after = np.asarray([row["directions"][i] for row in result["matched_rays"]])
        if surface["kind"] != 2:
            nn = indices[surface["material"]]
            if abs(nn-n)<1e-14:
                before=after; continue
            normal = n*before - nn*after
            normal /= np.linalg.norm(normal, axis=1)[:, None]
            ci = np.abs(np.sum(before*normal, axis=1)); ct = np.abs(np.sum(after*normal, axis=1))
            rs = (n*ci-nn*ct)/(n*ci+nn*ct); rp = (nn*ci-n*ct)/(nn*ci+n*ct)
            power *= (1-(rs*rs+rp*rp)/2) * surface["transmission"]
            n = nn
        before = after
    result.update(points=points, direction=direction, opl=opl, power=power, launch=launch, vertices=vertices)
    return result

def sphere_opd(reference, radius, chief=0):
    center = reference["points"][chief, -1]
    exit = reference["points"][:, -2]
    direction = reference["direction"]
    distance = []
    for point, ray in zip(exit, direction):
        delta = point-center
        roots = np.roots([np.dot(ray, ray), 2*np.dot(delta, ray), np.dot(delta, delta)-radius*radius])
        assert np.max(np.abs(roots.imag)) < 1e-9
        distance.append(min(roots.real, key=abs))
    phase = np.zeros(len(exit))
    if reference["infinite"]:
        origins = np.asarray([row["origin_mm"] for row in reference["launch"]["rays"]])
        phase = (origins-origins[chief]) @ np.asarray(reference["launch"]["rays"][chief]["direction"])
    value = reference["opl"] + reference["exit_index"]*np.asarray(distance) + phase
    return value-value[chief]

def channel(project, grid, name):
    step = 2/(grid-1)
    pupils = [[0., 0.]] + [[x, y] for y in np.linspace(-1,1,grid) for x in np.linspace(-1,1,grid)
                          if x*x+y*y <= 1+1e-12]
    count = len(pupils)
    pupils += [[x+1e-4, y] for x,y in pupils[1:count]] + [[x,y+1e-4] for x,y in pupils[1:count]]
    ref = external(project, pupils, name)
    ref["infinite"] = project.system["objectDistance"] == 0
    pupil_z = ref["vertices"][-1] + ref["exit_pupil_distance_mm"]
    exits = ref["points"][:, -2]; directions = ref["direction"]
    mapped = exits + directions*((pupil_z-exits[:,2])/directions[:,2])[:,None]
    relative = mapped[1:count, :2] - mapped[0,:2]
    u = (mapped[count:count*2-1]-mapped[1:count])/1e-4
    v = (mapped[count*2-1:]-mapped[1:count])/1e-4
    jac = np.linalg.det(np.stack([u[:,:2],v[:,:2]],axis=-1))
    assert np.min(jac) > 0
    radius = np.linalg.norm(ref["points"][0,-1]-mapped[0])
    phase = sphere_opd(ref, radius)[1:count]
    legacy = sphere_opd(ref, np.linalg.norm(ref["points"][0,-1]-exits[0]))[1:count]
    legacy -= np.average(legacy, weights=ref["power"][1:count])
    native = project._request("wavefront", pupil_grid=grid)
    ours = np.asarray([sample["opd_mm"] for sample in native["samples"]])
    assert len(ours) == len(legacy)
    return dict(xy=relative, radius=radius, wave=project.system["wavelengths"][0][0], index=ref["exit_index"],
                amplitude=np.sqrt(ref["power"][1:count]*jac), phase=phase,
                power=np.sum(ref["power"][1:count]), center=ref["points"][0,-1], step=step,
                opd_error=float(np.max(np.abs(ours-legacy))),
                rms_error=abs(native["rms_mm"]-float(np.sqrt(np.average(legacy**2,weights=ref["power"][1:count])))))

def reference_image(channels, weights, size, primary):
    pixel = min(c["wave"]*c["radius"]/(4*c["index"]*np.max(np.linalg.norm(c["xy"],axis=1))) for c in channels)
    coordinates = (np.arange(size)-size//2)*pixel*1e-3
    xx,yy = np.meshgrid(coordinates, coordinates)
    result = np.zeros((size,size)); energy=[]; total = 0
    for c,weight in zip(channels,weights):
        offset = c["center"]-channels[primary]["center"]
        locations = np.column_stack([xx.ravel()-offset[0],yy.ravel()-offset[1]])
        k = 2*np.pi*c["index"]/(c["wave"]*1e-3*c["radius"])
        wave = c["amplitude"]*np.exp(2j*np.pi*c["phase"]/(c["wave"]*1e-3))
        # Small chunks bound memory without sharing the C++ separable algorithm.
        intensity = np.concatenate([np.abs(np.exp(-1j*k*(part @ c["xy"].T)) @ wave)**2
                                    for part in np.array_split(locations,32)]).reshape(size,size)
        fraction = intensity.sum()*(pixel*1e-3)**2*c["step"]**2 / ((c["wave"]*1e-3*c["radius"]/c["index"])**2*c["power"])
        energy.append(float(fraction))
        strength = c["power"]*weight
        result += intensity/intensity.sum()*strength; total += strength
    result /= total
    otf=np.abs(np.fft.fft2(result)); otf/=otf[0,0]
    return dict(psf=result, pixel=pixel, mtf_x=otf[0,:size//2+1], mtf_y=otf[:size//2+1,0], energy=energy)

cases = [("singlet", ROOT/"tests/data/geopter_singlet.json", 1., 0., 0., 1.),
         ("doublet", ROOT/"examples/geopter/kingslake_doublet.json", 2., 1., 0., 1.),
         ("triplet", ROOT/"examples/geopter/sasian_triplet_glass.json", 2., 1., 0., 1.),
         ("double_gauss", ROOT/"examples/geopter/dbgauss.json", 4., 1., 0., 1.),
         ("aspheric", ROOT/"examples/geopter/aspheric_singlet.json", 1., 1., 0., 1.),
         ("finite_doublet", ROOT/"examples/geopter/kingslake_doublet.json", 1., .5, 200., 1.),
         ("immersed_singlet", ROOT/"tests/data/geopter_singlet.json", .5, .5, 200., 1.33)]
summary=[]
for name,source,diameter,field,distance,medium in cases:
    project=Project.import_geopter(source, executable=binary)
    project.system.update(pupilDiameter=diameter, fields=[[0,field,1]], objectDistance=distance,
                          wavelengths=[[.4861327,1],[.5875618,2],[.6562725,1]], primary=1)
    if medium != 1:
        glass=copy.deepcopy(next(g for g in project.data["materials"] if g["name"]=="AIR"))
        glass.update(name="EXIT_CONSTANT",nd=medium);project.data["materials"].append(glass)
        project.system["surfaces"][-1]["material"]="EXIT_CONSTANT"
    project.autofocus()
    per_grid=[]
    for grid in (33,49,65):
        channels=[]
        for wi,wave in enumerate(project.system["wavelengths"]):
            mono=Project(project.data, executable=binary)
            mono.system["wavelengths"]=[wave];mono.system["primary"]=0
            channels.append(channel(mono,grid,f"{name}-{grid}-{wi}"))
        ref=reference_image(channels,[w[1] for w in project.system["wavelengths"]],64,1)
        actual=project.diffraction(size=64,pupil_grid=grid)
        psf=np.asarray(actual["psf"]).reshape(64,64)
        row=dict(grid=grid, pixel_error_um=abs(actual["pixel_um"]-ref["pixel"]),
                 psf_l1_error=float(np.abs(psf-ref["psf"]).sum()),
                 mtf_max_error=max(float(np.max(np.abs(np.asarray(actual[k])-ref[k]))) for k in ("mtf_x","mtf_y")),
                 opd_max_error_mm=max(c["opd_error"] for c in channels),
                 opd_rms_error_mm=max(c["rms_error"] for c in channels),
                 window_energy_fractions=ref["energy"],
                 outer_border_fraction=float(psf[0,:].sum()+psf[-1,:].sum()+psf[1:-1,0].sum()+psf[1:-1,-1].sum()))
        row["passed"]=bool(row["pixel_error_um"]<1e-7 and row["psf_l1_error"]<1e-5 and row["mtf_max_error"]<1e-5
                       and row["opd_max_error_mm"]<1e-7 and row["opd_rms_error_mm"]<1e-7
                       and all(.95<e<1.03 for e in ref["energy"]))
        per_grid.append((row,actual))
        print(name, json.dumps(row), flush=True)
    before,after=per_grid[-2][1],per_grid[-1][1]
    frequencies=np.asarray(after["frequency_per_mm"])
    band=frequencies <= .7*frequencies[-1]
    convergence=max(float(np.max(np.abs(np.interp(frequencies[band],before["frequency_per_mm"],before[k])-np.asarray(after[k])[band])))
                    for k in ("mtf_x","mtf_y"))
    small=project.diffraction(size=32,pupil_grid=65)
    window_error=max(float(np.max(np.abs(np.interp(np.asarray(small["frequency_per_mm"])[1:12],after["frequency_per_mm"],after[k])-np.asarray(small[k])[1:12])))
                     for k in ("mtf_x","mtf_y"))
    entry=dict(system=name,pupil_diameter_mm=diameter,field_deg=field,object_distance_mm=distance,exit_index=medium,
               grids=[v[0] for v in per_grid],mtf_49_to_65_max_error=convergence,mtf_window_32_to_64_max_error=window_error,
               passed=all(v[0]["passed"] for v in per_grid) and convergence<.035 and window_error<.035)
    summary.append(entry)
    (target/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
if not all(row["passed"] for row in summary): raise SystemExit(1)
