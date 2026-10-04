"""Independent 2-D analytic reference; uses only Python's standard library.

Input: numeric Kingslake doublet data from the pinned Geopter example.
No Optical CAD or Geopter calculation code is imported. This is a reference
for our documented Cauchy nd:Vd interpretation, not a recorded Geopter run.
"""
import json
import math
from pathlib import Path

source = Path(__file__).resolve().parents[1] / "examples/geopter/kingslake_doublet.json"
data = json.loads(source.read_text())
wavelength = data["Spec"]["Wvl"]["Value"][0] / 1000

def index(name):
    if name == "AIR":
        return 1.
    nd, vd = map(float, name.split(":"))
    f, d, cw = .4861327, .5875618, .6562725
    b = (nd - 1) / vd / (1 / f**2 - 1 / cw**2)
    return nd + b * (1 / wavelength**2 - 1 / d**2)

# Reduced-angle ABCD: refraction power and translation t/n.
a, b, c, d = 1., 0., 0., 1.
z, medium = 0., 1.
y, ray_z, theta, opl = 2., -30., 0., 0.
hits = []
for i in range(1, 5):
    row = data["Assembly"][str(i)]
    cv, t = row["Curvature"], row["Thickness"]
    next_n = index(row["Material"])
    power = (next_n - medium) * cv
    c, d = c - power*a, d - power*b
    if i != 4:
        a, b = a + t/next_n*c, b + t/next_n*d
    # Sphere-ray intersection solved analytically, then Snell's law on signed angles.
    dy, dz = math.sin(theta), math.cos(theta)
    if cv == 0:
        distance = (z-ray_z)/dz
    else:
        r = 1/cv
        center = z+r
        qb = 2*(y*dy + (ray_z-center)*dz)
        qc = y*y + (ray_z-center)**2-r*r
        discriminant = qb*qb-4*qc
        candidates = []
        for distance in ((-qb-math.sqrt(discriminant))/2, (-qb+math.sqrt(discriminant))/2):
            yy, zz = y+distance*dy, ray_z+distance*dz
            cap_z = z+r-math.copysign(math.sqrt(max(0,r*r-yy*yy)),r)
            if distance >= -1e-10 and abs(zz-cap_z)<1e-8:
                candidates.append(distance)
        distance = min(candidates)
    y, ray_z = y+distance*dy, ray_z+distance*dz
    opl += medium * distance
    normal_angle = -math.asin(y*cv)
    theta = normal_angle + math.asin(medium/next_n * math.sin(theta-normal_angle))
    hits.append([y, ray_z])
    medium = next_n
    z += t
image_y = y+(z-ray_z)*math.tan(theta)
print(json.dumps({"wavelength_um":wavelength,"matrix":[a,b,c,d],"efl_mm":-1/c,
                  "bfl_mm":-a/c,"hits_yz_mm":hits,"opl_to_last_surface_mm":opl,
                  "image_y_mm":image_y,"image_z_mm":z},indent=2))
