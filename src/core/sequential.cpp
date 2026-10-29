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
    if(hasCoordinates(*this))throw std::invalid_argument("У локального изображения нет единой глобальной Z; используйте sequentialFrames().image");
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
    if(usesPhysicalStop(*this) && fieldType!=FieldType::Angle && fieldType!=FieldType::ObjectHeight)out.push_back("Локальные системы поддерживают углы и высоты объекта; высоты изображения ещё не поддержаны");
    for (auto f : fields) {
        if (!validField(*this, f)) out.push_back("Некорректное поле зрения / тип поля / расстояние объекта");
        else if (fieldType != FieldType::Angle) {
            try { angularField(*this, catalog, f); }
            catch (const std::exception& e) { out.push_back(e.what()); }
        }
    }
    for (auto w : wavelengths)
        if (!std::isfinite(w.um) || w.um < 0.2 || w.um > 5 || !std::isfinite(w.weight) ||
            w.weight <= 0)
            out.push_back("Некорректная длина волны");
    double fieldWeight = 0, waveWeight = 0;
    for (const auto& f : fields) fieldWeight += f.weight;
    for (const auto& w : wavelengths) waveWeight += w.weight;
    if (!std::isfinite(fieldWeight) || !std::isfinite(waveWeight))
        out.push_back("Сумма весов полей или волн переполняется");
    for (size_t i = 0; i < surfaces.size(); ++i) {
        auto& s = surfaces[i];
        if (int(s.kind) < 0 || int(s.kind) > int(SurfaceKind::Stop)) out.push_back("Неизвестный тип поверхности");
        if (!std::isfinite(s.radius) || !std::isfinite(s.thickness) || s.thickness < 0 ||
            !std::isfinite(s.semiDiameter) || s.semiDiameter <= 0 || !std::isfinite(s.conic) ||
            !finite(s.tilt) || !finite(s.decenter) || !std::isfinite(s.transmission) ||
            s.transmission < 0 || s.transmission > 1 || !std::isfinite(s.reflectivity) ||
            s.reflectivity < 0 || s.reflectivity > 1)
            out.push_back("Некорректная геометрия поверхности " + std::to_string(i + 1));
        if(int(s.profile)<0 || int(s.profile)>4 || !std::isfinite(s.rotationRadius) ||
           (s.profile!=SurfaceProfile::Toroidal && s.rotationRadius!=0))out.push_back("Некорректный профиль поверхности");
        bool xy=false;for(double a:s.xySag){xy|=a!=0;if(!std::isfinite(a))out.push_back("Некорректный коэффициент XY");}
        if((s.profile!=SurfaceProfile::XYPolynomial&&xy)||(s.profile==SurfaceProfile::XYPolynomial&&(s.radius!=0||s.conic!=0)))out.push_back("XY полином требует плоскую базу R=0, k=0; коэффициенты разрешены только у XY");
        if(s.profile!=SurfaceProfile::Standard) {
            bool poly=false;for(double a:s.asphere)poly|=a!=0;for(double a:s.oddAsphere)poly|=a!=0;
            if(poly || (s.profile==SurfaceProfile::Toroidal && s.conic!=0))out.push_back("Новый профиль не поддерживает эти асферические коэффициенты/конику");
            for(int k=0;k<64;++k){const double angle=2*pi*k/64;try{if(!std::isfinite(sag(s,s.semiDiameter*cos(angle),s.semiDiameter*sin(angle))))throw std::invalid_argument("domain");surfaceNormal(s,s.semiDiameter*cos(angle),s.semiDiameter*sin(angle));}catch(...){out.push_back("Механический полудиаметр выходит из действительной области профиля");break;}}
        }
        const auto& mask=s.aperture;
        if(int(mask.shape)<0 || int(mask.shape)>2 || !std::isfinite(mask.halfX) || !std::isfinite(mask.halfY) ||
           mask.halfX<=0 || mask.halfY<=0 || !std::isfinite(mask.obscuration) || mask.obscuration<0 || mask.obscuration>=s.semiDiameter)
            out.push_back("Некорректная апертура или экранирование");
        if(s.profile==SurfaceProfile::Standard && s.radius!=0 && 1+s.conic>0 &&
           (mask.shape!=ApertureShape::Circle || mask.obscuration>0)) {
            const double domain=std::abs(s.radius)/std::sqrt(1+s.conic);
            const double extent=mask.shape==ApertureShape::Rectangle?std::hypot(mask.halfX,mask.halfY):std::max(mask.halfX,mask.halfY);
            if(mask.obscuration>=std::min(domain,s.semiDiameter) || (mask.shape!=ApertureShape::Circle && extent>=domain))out.push_back("Маска выходит за реальную область поверхности или закрывает весь просвет");
        }
        if(mask.shape!=ApertureShape::Circle) {
            const double extent=mask.shape==ApertureShape::Rectangle?std::hypot(mask.halfX,mask.halfY):std::max(mask.halfX,mask.halfY);
            if(extent>s.semiDiameter+1e-12 || mask.obscuration>=std::min(mask.halfX,mask.halfY))out.push_back("Маска должна помещаться в механический круг и иметь открытый просвет");
        }
        for (double a : s.asphere)
            if (!std::isfinite(a))
                out.push_back("Некорректная асферика");
        for (double a : s.oddAsphere)
            if (!std::isfinite(a)) out.push_back("Некорректная нечётная асферика");
        try {
            for (auto w : wavelengths)
                catalog.get(s.material).index(w.um);
        } catch (const std::exception& e) {
            out.push_back(e.what());
        }
    }
    try {sequentialFrames(*this);}catch(const std::exception& e){out.push_back(e.what());}
    const auto clear = clearSemiDiameters(*this);
    for (size_t i = 0; i < surfaces.size(); ++i)
        if (!(clear[i] > 1e-9) || !std::isfinite(clear[i]) ||
            !std::isfinite(sag(surfaces[i], clear[i], 0)))
            out.push_back("Нет действительной центральной апертуры поверхности " + std::to_string(i + 1));
    return out;
}
double sag(const Surface& s, double x, double y) {
    if(s.profile!=SurfaceProfile::Standard)return shapedSag(s,x,y);
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
    qp = q * sqrt(q);
    for (double a : s.oddAsphere) { z += a * qp; qp *= q; }
    return z;
}
static Vec3 sagNormal(const Surface& s, Vec3 p) {
    if(s.profile!=SurfaceProfile::Standard)return shapedNormal(s,p.x,p.y);
    double q = p.x * p.x + p.y * p.y, derivative = 0;
    if (s.radius != 0) {
        double rad = 1 - (1 + s.conic) * q / (s.radius * s.radius);
        if (rad <= 0)
            return {0, 0, 1};
        derivative = 1 / (s.radius * sqrt(rad));
    }
    double qp = q;
    for (size_t i = 0; i < s.asphere.size(); ++i) {
        derivative += 2 * (i + 2) * s.asphere[i] * qp;
        qp *= q;
    }
    qp = sqrt(q);
    for (size_t i = 0; i < s.oddAsphere.size(); ++i) {
        derivative += (2 * i + 3) * s.oddAsphere[i] * qp;
        qp *= q;
    }
    return Vec3{-derivative * p.x, -derivative * p.y, 1}.unit();
}
Vec3 surfaceNormal(const Surface& s,double x,double y){return sagNormal(s,{x,y,0});}
std::optional<SurfaceHit> intersectFramedSurface(const Surface& s, const RigidFrame& pose, const Ray& ray,
                                           bool aperture) {
    if(s.profile==SurfaceProfile::Toroidal)return intersectToroidal(s,pose,ray,aperture);
    if(s.profile==SurfaceProfile::XYPolynomial)return intersectXYPolynomial(s,pose,ray,aperture);
    Vec3 o = pose.local(ray.origin), d = pose.inverseRotate(ray.direction);
    std::vector<double> roots;
    bool poly = false;
    for (double a : s.asphere)
        poly |= a != 0;
    for (double a : s.oddAsphere) poly |= a != 0;
    if (!poly) {
        if (s.radius == 0) {
            if (std::abs(d.z) < 1e-13)
                return {};
            roots.push_back(-o.z / d.z);
        } else {
            const double mx=s.profile==SurfaceProfile::CylinderY?0:1,my=s.profile==SurfaceProfile::CylinderX?0:1;
            double k = 1 + s.conic, A = mx*d.x * d.x + my*d.y * d.y + k * d.z * d.z,
                   B = 2 * (mx*o.x * d.x + my*o.y * d.y - s.radius * d.z + k * o.z * d.z),
                   C = mx*o.x * o.x + my*o.y * o.y - 2 * s.radius * o.z + k * o.z * o.z;
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
        if (aperture && !insideSurfaceAperture(s,p.x,p.y))
            continue;
        return SurfaceHit{pose.world(p), pose.rotate(sagNormal(s, p)), std::max(0.0, t)};
    }
    return {};
}
std::optional<SurfaceHit> intersectSurface(const Surface& s,const Pose& pose,const Ray& ray,bool aperture) {
    return intersectFramedSurface(s,RigidFrame::fromPose(pose),ray,aperture);
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
    const auto clear = aperture ? clearSemiDiameters(sys) : std::vector<double>{};
    try {
        if (!finite(ray.origin) || !finite(ray.direction) || !std::isfinite(ray.power) || ray.power < 0 ||
            !std::isfinite(ray.wavelength) || ray.wavelength < .2 || ray.wavelength > 5 || sys.surfaces.empty())
            throw std::invalid_argument("Invalid ray");
        ray.direction = ray.direction.unit();
        const auto frames=sequentialFrames(sys);
        for (size_t i = 0; i < sys.surfaces.size() && i <= through; ++i) {
            auto& s = sys.surfaces[i];
            const auto& p=frames.surfaces.at(i);
            const bool advanced=s.profile!=SurfaceProfile::Standard || s.aperture.shape!=ApertureShape::Circle || s.aperture.obscuration>0;
            // A blocked first geometric contact cannot tunnel through an opaque
            // mask and reappear at a later open root of the same surface.
            auto hit = intersectFramedSurface(s, p, ray, advanced?false:aperture);
            if (!hit) {
                out.failedSurface = i;
                out.status =
                    intersectFramedSurface(s, p, ray, false) ? TraceStatus::Vignetted : TraceStatus::Miss;
                return out;
            }
            out.opl += n * hit->distance;
            if (aperture) {
                const auto local = p.local(hit->point);
                if (!insideSurfaceAperture(s,local.x,local.y) || std::hypot(local.x, local.y) > clear[i] + 1e-9) {
                    out.status = TraceStatus::Vignetted; out.failedSurface = i; return out;
                }
            }
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
            const auto origin=frames.image.local(ray.origin),direction=frames.image.inverseRotate(ray.direction);
            if (std::abs(direction.z) < 1e-12) {
                out.status = TraceStatus::Miss;
                return out;
            }
            double t = -origin.z / direction.z;
            if (t < 0) {
                out.status = TraceStatus::Miss;
                return out;
            }
            out.image = ray.origin + ray.direction * t;
            out.imageLocal=frames.image.local(out.image);
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
    if (s.stop >= s.surfaces.size() || !std::isfinite(s.pupilDiameter) || s.pupilDiameter <= 0)
        throw std::invalid_argument("Invalid stop or entrance pupil");
    f = angularField(s, c, f);
    const auto pupil = vignettedPupil(f, px, py);
    px = pupil.x;
    py = pupil.y;
    auto vertices = s.vertices();
    double start = s.objectDistance > 0 ? -s.objectDistance : -std::max(30.0, s.pupilDiameter * 2);
    Vec3 direction = Vec3{tan(f.x * deg), tan(f.y * deg), 1}.unit();
    double A = 1, B = 0, C = 0, D = 1, n = 1;
    bool centered = !usesPhysicalStop(s);
    for (size_t i = 0; centered && i < s.stop; ++i) {
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
    const auto frames=sequentialFrames(s);
    const bool localCoordinates=hasCoordinates(s);
    if(localCoordinates && s.objectDistance==0) {
        const auto& frame=frames.surfaces.front();const double r=clearSemiDiameters(s).front();
        const double margin=r*(std::abs(frame.basis[0].z)+std::abs(frame.basis[1].z))+std::abs(sag(s.surfaces.front(),r,0)*frame.basis[2].z);
        start=std::min(start,frame.position.z-margin-1);
    }
    const double stopZ = localCoordinates?0:vertices.at(s.stop) + s.surfaces.at(s.stop).decenter.z;
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
    const auto& stopPose=frames.surfaces.at(s.stop);
    if(localCoordinates){const auto target=stopPose.world({px*physicalRadius,py*physicalRadius,0});ax=target.x;ay=target.y;}
    auto residual = [&](double x, double y) -> std::optional<Vec3> {
        auto t = trace(s, c, make(x, y), false, s.stop, false);
        if (t.status != TraceStatus::Complete)
            return {};
        return stopPose.local(t.exitPoint) - Vec3{px * physicalRadius, py * physicalRadius, 0};
    };
    for (int i = 0; i < 30; ++i) {
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
        const double dx = (d * e->x - b * e->y) / det, dy = (-cc * e->x + a * e->y) / det;
        bool improved = false;
        for (double scale = 1; scale >= 1. / 128; scale /= 2) {
            auto trial = residual(ax - scale * dx, ay - scale * dy);
            if (trial && std::hypot(trial->x, trial->y) < std::hypot(e->x, e->y)) {
                ax -= scale * dx; ay -= scale * dy; improved = true; break;
            }
        }
        if (!improved) break;
    }
    const auto final = residual(ax, ay);
    // Unaimed rays must never masquerade as samples at the requested pupil point.
    // An invalid ray is counted as lost by analyses, while successful samples remain usable.
    if (!final || std::hypot(final->x, final->y) > 1e-7)
        return {{std::numeric_limits<double>::quiet_NaN(), 0, 0}, {}, w};
    return make(ax, ay);
}
double apertureDiameter(const SequentialSystem& sys, const Catalog& c, ApertureType type, double value) {
    if (!std::isfinite(value) || value <= 0) throw std::invalid_argument("Апертура должна быть положительной");
    if (type == ApertureType::EntranceDiameter) return value;
    auto s = resolvedSystem(sys, c);
    const double w = s.wavelengths.at(s.primary).um;
    const auto p = paraxial(s, c, w);
    if (type == ApertureType::FNumber) return std::abs(p.efl) / value;
    if (type == ApertureType::ObjectNA) {
        if (s.objectDistance <= 0 || value >= 1)
            throw std::invalid_argument("NA объекта: нужен конечный объект в воздухе и 0 < NA < 1");
        return 2 * s.objectDistance * tan(asin(value));
    }
    if (type != ApertureType::ImageNA) throw std::invalid_argument("Неизвестный тип апертуры");
    double n = 1;
    for (const auto& surface : s.surfaces)
        if (surface.kind == SurfaceKind::Refract) n = c.get(surface.material).index(w);
    if (value >= n) throw std::invalid_argument("NA изображения должна быть меньше показателя выходной среды");
    const double power = std::abs(p.matrix[2] + (s.objectDistance > 0 ? p.matrix[3] / s.objectDistance : 0));
    if (power < 1e-12) throw std::invalid_argument("NA не определена для коллимированного выходного пучка");
    return 2 * n * tan(asin(value / n)) / power;
}
Paraxial paraxial(const SequentialSystem& s, const Catalog& cat, double w) {
    if (!s.solves.empty()) return paraxial(resolvedSystem(s, cat), cat, w);
    if(usesPhysicalStop(s))throw std::invalid_argument("Для локальных/неосесимметричных систем используйте дифференциальный первый порядок");
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
    if(hasCoordinates(s))throw std::invalid_argument("Автофокус локальных систем ещё не поддержан; оптимизируйте последний промежуток по RMS");
    if (s.surfaces.empty() || s.fields.empty() || s.primary >= s.wavelengths.size())
        throw std::invalid_argument("Autofocus requires surfaces, fields and a primary wavelength");
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
