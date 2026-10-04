#include "optics/model.hpp"
#include <limits>

namespace optics {
double SequentialSystem::imageZ(const Catalog& c) const {
    return solves.empty() ? imageZ() : resolvedSystem(*this, c).imageZ();
}
std::vector<double> SequentialSystem::vertices(const Catalog& c) const {
    return solves.empty() ? vertices() : resolvedSystem(*this, c).vertices();
}
double SequentialSystem::imageZ() const {
    if (!solves.empty()) return resolvedSystem(*this).imageZ();
    double z = defocus;
    for (auto& s : surfaces)
        z += s.thickness;
    return z;
}
std::vector<double> SequentialSystem::vertices() const {
    if (!solves.empty()) return resolvedSystem(*this).vertices();
    std::vector<double> out;
    double z = 0;
    for (auto& s : surfaces) {
        out.push_back(z);
        z += s.thickness;
    }
    return out;
}
SequentialSystem SequentialSystem::demo() {
    SequentialSystem s;
    Surface front;
    front.name = "Линза · передняя";
    front.radius = 50;
    front.thickness = 5;
    front.material = "N-BK7";
    Surface back;
    back.name = "Линза · задняя";
    back.radius = -50;
    back.thickness = 46.7;
    s.surfaces = {front, back};
    return s;
}
std::vector<std::string> SequentialSystem::validate(const Catalog& catalog) const {
    if (!solves.empty()) {
        try { return resolvedSystem(*this, catalog).validate(catalog); }
        catch (const std::exception& e) { return {e.what()}; }
    }
    std::vector<std::string> out;
    if (surfaces.empty() || surfaces.size() > 500)
        out.push_back("Нужно от 1 до 500 поверхностей");
    if (stop >= surfaces.size())
        out.push_back("Диафрагма не существует");
    if (!std::isfinite(pupilDiameter) || pupilDiameter <= 0 || pupilDiameter > 1e5)
        out.push_back("Некорректный диаметр зрачка");
    if (!std::isfinite(objectDistance) || objectDistance < 0 || !std::isfinite(defocus))
        out.push_back("Некорректное положение объекта / дефокус");
    if (fields.empty() || fields.size() > 50 || wavelengths.empty() || wavelengths.size() > 20 ||
        primary >= wavelengths.size())
        out.push_back("Некорректные поля или длины волн");
    if (pupilGrid < 3 || pupilGrid > 65)
        out.push_back("Сетка зрачка должна быть 3…65");
    for (auto f : fields)
        if (!finite({f.x, f.y, f.weight}) || std::abs(f.x) > 80 || std::abs(f.y) > 80 ||
            f.weight <= 0 || !validVignetting(f))
            out.push_back("Некорректное поле зрения");
    for (auto w : wavelengths)
        if (!std::isfinite(w.um) || w.um < 0.2 || w.um > 5 || !std::isfinite(w.weight) ||
            w.weight <= 0)
            out.push_back("Некорректная длина волны");
    for (size_t i = 0; i < surfaces.size(); ++i) {
        auto& s = surfaces[i];
        if (!std::isfinite(s.radius) || !std::isfinite(s.thickness) || s.thickness < 0 ||
            !std::isfinite(s.semiDiameter) || s.semiDiameter <= 0 || !std::isfinite(s.conic) ||
            !finite(s.tilt) || !finite(s.decenter) || !std::isfinite(s.transmission) ||
            s.transmission < 0 || s.transmission > 1 || !std::isfinite(s.reflectivity) ||
            s.reflectivity < 0 || s.reflectivity > 1)
            out.push_back("Некорректная геометрия поверхности " + std::to_string(i + 1));
        for (double a : s.asphere)
            if (!std::isfinite(a))
                out.push_back("Некорректная асферика");
        if (s.radius != 0 &&
            1 - (1 + s.conic) * s.semiDiameter * s.semiDiameter / (s.radius * s.radius) < 0)
            out.push_back("Апертура выходит за действительную область поверхности " +
                          std::to_string(i + 1));
        try {
            for (auto w : wavelengths)
                catalog.get(s.material).index(w.um);
        } catch (const std::exception& e) {
            out.push_back(e.what());
        }
    }
    for (size_t i = 0; i + 1 < surfaces.size(); ++i) {
        auto& a = surfaces[i];
        auto& b = surfaces[i + 1];
        if (a.decenter.norm2() != 0 || b.decenter.norm2() != 0 || a.tilt.norm2() != 0 ||
            b.tilt.norm2() != 0 || a.kind == SurfaceKind::Mirror || b.kind == SurfaceKind::Mirror)
            continue;
        double radius = std::min(a.semiDiameter, b.semiDiameter);
        for (int j = 0; j <= 16; ++j) {
            double y = radius * j / 16;
            if (a.thickness + sag(b, 0, y) - sag(a, 0, y) < -1e-7) {
                out.push_back("Поверхности пересекаются: " + std::to_string(i + 1) + " / " +
                              std::to_string(i + 2));
                break;
            }
        }
    }
    return out;
}
double sag(const Surface& s, double x, double y) {
    double q = x * x + y * y, z = 0;
    if (s.radius != 0) {
        double rad = 1 - (1 + s.conic) * q / (s.radius * s.radius);
        if (rad < 0)
            return std::numeric_limits<double>::quiet_NaN();
        z = q / (s.radius * (1 + sqrt(rad)));
    }
    double qp = q * q;
    for (double a : s.asphere) {
        z += a * qp;
        qp *= q;
    }
    return z;
}
static Vec3 sagNormal(const Surface& s, Vec3 p) {
    double q = p.x * p.x + p.y * p.y, derivative = 0;
    if (s.radius != 0) {
        double rad = 1 - (1 + s.conic) * q / (s.radius * s.radius);
        if (rad <= 0)
            return {0, 0, 1};
        derivative = 1 / (s.radius * sqrt(rad));
    }
    double qp = q;
    for (size_t i = 0; i < 4; ++i) {
        derivative += 2 * (i + 2) * s.asphere[i] * qp;
        qp *= q;
    }
    return Vec3{-derivative * p.x, -derivative * p.y, 1}.unit();
}
std::optional<SurfaceHit> intersectSurface(const Surface& s, const Pose& pose, const Ray& ray,
                                           bool aperture) {
    Vec3 o = pose.local(ray.origin), d = pose.inverseRotate(ray.direction);
    std::vector<double> roots;
    bool poly = false;
    for (double a : s.asphere)
        poly |= a != 0;
    if (!poly) {
        if (s.radius == 0) {
            if (std::abs(d.z) < 1e-13)
                return {};
            roots.push_back(-o.z / d.z);
        } else {
            double k = 1 + s.conic, A = d.x * d.x + d.y * d.y + k * d.z * d.z,
                   B = 2 * (o.x * d.x + o.y * d.y - s.radius * d.z + k * o.z * d.z),
                   C = o.x * o.x + o.y * o.y - 2 * s.radius * o.z + k * o.z * o.z;
            if (std::abs(A) < 1e-14) {
                if (std::abs(B) > 1e-14)
                    roots.push_back(-C / B);
            } else {
                double disc = B * B - 4 * A * C;
                if (disc < 0)
                    return {};
                double q = -0.5 * (B + std::copysign(sqrt(disc), B));
                if (std::abs(q) > 1e-15) {
                    roots.push_back(q / A);
                    roots.push_back(C / q);
                } else
                    roots.push_back(-B / (2 * A));
            }
        }
    } else {
        if (std::abs(d.z) < 1e-13)
            return {};
        double t = -o.z / d.z;
        for (int i = 0; i < 40; ++i) {
            Vec3 p = o + d * t;
            double z = sag(s, p.x, p.y);
            if (!std::isfinite(z))
                return {};
            double f = p.z - z;
            if (std::abs(f) < 1e-9) {
                roots.push_back(t);
                break;
            }
            Vec3 n = sagNormal(s, p);
            double der = d.dot(n) / n.z;
            if (std::abs(der) < 1e-13)
                return {};
            t -= f / der;
        }
    }
    std::sort(roots.begin(), roots.end());
    for (double t : roots) {
        if (t < -1e-7 || !std::isfinite(t))
            continue;
        Vec3 p = o + d * t;
        double zs = sag(s, p.x, p.y);
        if (!std::isfinite(zs) || std::abs(zs - p.z) > 1e-6)
            continue;
        if (aperture && p.x * p.x + p.y * p.y > s.semiDiameter * s.semiDiameter + 1e-9)
            continue;
        return SurfaceHit{pose.world(p), pose.rotate(sagNormal(s, p)), std::max(0.0, t)};
    }
    return {};
}
RayTrace trace(const SequentialSystem& sys, const Catalog& cat, Ray ray, bool toImage,
               size_t through, bool aperture) {
    if (!sys.solves.empty()) {
        try { return trace(resolvedSystem(sys, cat), cat, ray, toImage, through, aperture); }
        catch (const std::exception&) {
            RayTrace failed;
            failed.points.push_back(ray.origin);
            return failed;
        }
    }
    RayTrace out;
    out.points.push_back(ray.origin);
    out.power = ray.power;
    double n = 1;
    auto z = sys.vertices();
    try {
        ray.direction = ray.direction.unit();
        for (size_t i = 0; i < sys.surfaces.size() && i <= through; ++i) {
            auto& s = sys.surfaces[i];
            Pose p{{s.decenter.x, s.decenter.y, z[i] + s.decenter.z}, s.tilt};
            auto hit = intersectSurface(s, p, ray, aperture);
            if (!hit) {
                out.failedSurface = i;
                out.status =
                    intersectSurface(s, p, ray, false) ? TraceStatus::Vignetted : TraceStatus::Miss;
                return out;
            }
            out.opl += n * hit->distance;
            out.points.push_back(hit->point);
            Vec3 next;
            if (s.kind == SurfaceKind::Mirror) {
                next = reflect(ray.direction, hit->normal);
                out.power *= s.reflectivity;
            } else if (s.kind == SurfaceKind::Stop)
                next = ray.direction;
            else {
                double nn = cat.get(s.material).index(ray.wavelength);
                if (!refract(ray.direction, hit->normal, n, nn, next)) {
                    out.status = TraceStatus::TotalInternalReflection;
                    out.failedSurface = i;
                    return out;
                }
                out.power *= (1 - fresnel(ray.direction, hit->normal, n, nn)) * s.transmission;
                n = nn;
            }
            ray.origin = hit->point;
            ray.direction = next;
        }
        out.exitPoint = ray.origin;
        out.exitDirection = ray.direction;
        out.index = n;
        if (toImage) {
            if (std::abs(ray.direction.z) < 1e-12) {
                out.status = TraceStatus::Miss;
                return out;
            }
            double t = (sys.imageZ() - ray.origin.z) / ray.direction.z;
            if (t < 0) {
                out.status = TraceStatus::Miss;
                return out;
            }
            out.image = ray.origin + ray.direction * t;
            out.points.push_back(out.image);
        }
        out.status = TraceStatus::Complete;
    } catch (const std::exception&) {
        out.status = TraceStatus::Invalid;
    }
    return out;
}
Ray pupilRay(const SequentialSystem& s, const Catalog& c, Field f, double w, double px, double py) {
    if (!s.solves.empty()) return pupilRay(resolvedSystem(s, c), c, f, w, px, py);
    const auto pupil = vignettedPupil(f, px, py);
    px = pupil.x;
    py = pupil.y;
    auto vertices = s.vertices();
    double start = s.objectDistance > 0 ? -s.objectDistance : -std::max(30.0, s.pupilDiameter * 2);
    Vec3 direction = Vec3{tan(f.x * deg), tan(f.y * deg), 1}.unit();
    double A = 1, B = 0, C = 0, D = 1, n = 1;
    bool centered = true;
    for (size_t i = 0; i < s.stop; ++i) {
        auto& a = s.surfaces[i];
        if (a.kind == SurfaceKind::Mirror || a.tilt.norm2() > 0 || a.decenter.norm2() > 0) {
            centered = false;
            break;
        }
        double next = a.kind == SurfaceKind::Stop ? n : c.get(a.material).index(w);
        double power = a.radius == 0 ? 0 : (next - n) / a.radius;
        C -= power * A;
        D -= power * B;
        n = next;
        double t = a.thickness / n;
        A += t * C;
        B += t * D;
    }
    // Convert the entrance pupil diameter to the physical stop footprint.
    double magnification =
        centered ? std::abs(A + (s.objectDistance > 0 ? B / s.objectDistance : 0)) : 1;
    if (magnification < 1e-10)
        throw std::invalid_argument("Stop is at a pupil singularity");
    double physicalRadius = s.pupilDiameter / 2 * magnification;
    double ax = px * physicalRadius, ay = py * physicalRadius;
    const double stopZ = vertices.at(s.stop) + s.surfaces.at(s.stop).decenter.z;
    auto make = [&](double x, double y) {
        Vec3 target{x, y, stopZ};
        Ray ray;
        if (s.objectDistance > 0) {
            ray.origin = {-s.objectDistance * tan(f.x * deg), -s.objectDistance * tan(f.y * deg),
                          start};
            ray.direction = (target - ray.origin).unit();
        } else {
            ray.direction = direction;
            ray.origin = target - direction * ((stopZ - start) / direction.z);
        }
        ray.wavelength = w;
        return ray;
    };
    Pose stopPose{{s.surfaces[s.stop].decenter.x, s.surfaces[s.stop].decenter.y, stopZ},
                  s.surfaces[s.stop].tilt};
    auto residual = [&](double x, double y) -> std::optional<Vec3> {
        auto t = trace(s, c, make(x, y), false, s.stop, false);
        if (t.status != TraceStatus::Complete)
            return {};
        return stopPose.local(t.exitPoint) - Vec3{px * physicalRadius, py * physicalRadius, 0};
    };
    for (int i = 0; i < 10; ++i) {
        auto e = residual(ax, ay);
        if (!e || std::hypot(e->x, e->y) < 1e-8)
            break;
        double h = 1e-4;
        auto ex = residual(ax + h, ay), ey = residual(ax, ay + h);
        if (!ex || !ey)
            break;
        double a = (ex->x - e->x) / h, b = (ey->x - e->x) / h, cc = (ex->y - e->y) / h,
               d = (ey->y - e->y) / h, det = a * d - b * cc;
        if (std::abs(det) < 1e-12)
            break;
        ax -= (d * e->x - b * e->y) / det;
        ay -= (-cc * e->x + a * e->y) / det;
    }
    return make(ax, ay);
}
Paraxial paraxial(const SequentialSystem& s, const Catalog& cat, double w) {
    if (!s.solves.empty()) return paraxial(resolvedSystem(s, cat), cat, w);
    double A = 1, B = 0, C = 0, D = 1, n = 1;
    for (size_t i = 0; i < s.surfaces.size(); ++i) {
        auto& surf = s.surfaces[i];
        if (surf.kind == SurfaceKind::Mirror || surf.tilt.norm2() > 0 || surf.decenter.norm2() > 0)
            throw std::invalid_argument("Paraxial analysis requires centered refracting surfaces");
        double next = surf.kind == SurfaceKind::Stop ? n : cat.get(surf.material).index(w),
               power = surf.radius == 0 ? 0 : (next - n) / surf.radius;
        C -= power * A;
        D -= power * B;
        n = next;
        if (i + 1 < s.surfaces.size()) {
            double t = surf.thickness / n;
            A += t * C;
            B += t * D;
        }
    }
    if (std::abs(C) < 1e-14)
        throw std::invalid_argument("Afocal system: focal length is undefined");
    return {-n / C, -n * A / C, std::abs(n / C) / s.pupilDiameter, {A, B, C, D}};
}
double autofocus(SequentialSystem& s, const Catalog& c) {
    if (!s.solves.empty()) {
        if (imageThicknessLinked(s))
            throw std::invalid_argument("Автофокус требует независимой толщины до изображения: удалите её связи");
        auto candidate = s;
        auto physical = resolvedSystem(s, c);
        const double z = autofocus(physical, c);
        candidate.surfaces.back().thickness = physical.surfaces.back().thickness;
        candidate.defocus = 0;
        applySolves(candidate, c);
        s = std::move(candidate);
        return z;
    }
    std::vector<Vec3> a, b;
    double base = s.vertices().back();
    auto field = s.fields.front();
    for (int iy = -6; iy <= 6; ++iy)
        for (int ix = -6; ix <= 6; ++ix) {
            double x = ix / 6.0, y = iy / 6.0;
            if (x * x + y * y > 1)
                continue;
            auto t = trace(s, c, pupilRay(s, c, field, s.wavelengths[s.primary].um, x, y), false);
            if (t.status != TraceStatus::Complete || std::abs(t.exitDirection.z) < 1e-10)
                continue;
            Vec3 slope = t.exitDirection / t.exitDirection.z;
            a.push_back(t.exitPoint + slope * (base - t.exitPoint.z));
            b.push_back(slope);
        }
    if (a.size() < 5)
        throw std::runtime_error("Insufficient rays for autofocus");
    Vec3 ca, cb;
    for (size_t i = 0; i < a.size(); ++i) {
        ca = ca + a[i];
        cb = cb + b[i];
    }
    ca = ca / a.size();
    cb = cb / b.size();
    double num = 0, den = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        Vec3 aa = a[i] - ca, bb = b[i] - cb;
        num += aa.x * bb.x + aa.y * bb.y;
        den += bb.x * bb.x + bb.y * bb.y;
    }
    if (den < 1e-15)
        throw std::runtime_error("Collimated rays cannot be focused");
    double distance = -num / den;
    if (distance < 0 || !std::isfinite(distance))
        throw std::runtime_error("Virtual focus is outside the image model");
    s.surfaces.back().thickness = distance;
    s.defocus = 0;
    return base + distance;
}
} // namespace optics
