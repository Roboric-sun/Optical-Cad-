#include "optics/model.hpp"
#include <chrono>
#include <numeric>
#include <random>

namespace optics {
double DetectorData::totalPower() const {
    return std::accumulate(watts.begin(), watts.end(), 0.0);
}
std::array<Vec3, 6> prismVertices(Vec3 size) {
    if (!finite(size) || size.x <= 0 || size.y <= 0 || size.z <= 0)
        throw std::invalid_argument("Prism dimensions must be finite and positive");
    double x = size.x / 2, y = size.y / 2, z = size.z / 2;
    return {Vec3{-x, -y, -z}, Vec3{x, -y, -z}, Vec3{0, -y, z},
            Vec3{-x, y, -z},  Vec3{x, y, -z},  Vec3{0, y, z}};
}
struct PrismPlane {
    Vec3 normal;
    double offset;
};
static std::array<PrismPlane, 5> prismPlanes(Vec3 size) {
    auto vertices = prismVertices(size);
    std::array<PrismPlane, 5> planes;
    for (size_t i = 0; i < planes.size(); ++i) {
        auto indices = prismFaces[i];
        Vec3 a = vertices[indices[0]], b = vertices[indices[1]], c = vertices[indices[2]];
        Vec3 normal = (b - a).cross(c - a).unit();
        planes[i] = {normal, normal.dot(a)};
    }
    return planes;
}
Scene Scene::prismDemo(const Catalog& catalog) {
    Scene scene;
    scene.name = "Спектральная призма 60°";
    SceneObject prism;
    prism.name = "Призма N-BK7 · 60°";
    prism.kind = ObjectKind::Prism;
    prism.size = {20, 16, 10 * sqrt(3.)};
    double incidence = asin(catalog.get(prism.material).index(.5875618) * .5);
    double beta = incidence - 30 * deg;
    Vec3 incoming{cos(beta), 0, sin(beta)}, outgoing{cos(beta), 0, -sin(beta)};
    for (double wave : {.4861327, .5875618, .6562725}) {
        Source source;
        source.name = "Спектральная линия " + std::to_string(wave * 1000) + " нм";
        source.pose.position = Vec3{-5, 0, 0} - incoming * 30;
        source.pose.tilt.y = 90 - beta / deg;
        source.coneAngle = .15;
        source.wavelength = wave;
        source.power = 1. / 3;
        scene.sources.push_back(source);
    }
    SceneObject detector;
    detector.name = "Детектор спектра";
    detector.kind = ObjectKind::Detector;
    detector.material = "AIR";
    detector.pose.position = Vec3{5, 0, 0} + outgoing * 40;
    detector.pose.tilt.y = 90 + beta / deg;
    detector.size = {4, 4, .1};
    detector.nx = detector.ny = 96;
    scene.objects = {prism, detector};
    return scene;
}
Scene Scene::demo() {
    Scene s;
    Source src;
    src.name = "LED · конус 12°";
    src.power = 1;
    s.sources.push_back(src);
    SceneObject lens;
    lens.name = "Коллимирующая линза";
    lens.kind = ObjectKind::Lens;
    lens.pose.position = {0, 0, 25};
    lens.size = {24, 24, 6};
    lens.radius1 = 30;
    lens.radius2 = -30;
    s.objects.push_back(lens);
    SceneObject detector;
    detector.name = "Детектор 1";
    detector.kind = ObjectKind::Detector;
    detector.pose.position = {0, 0, 80};
    detector.size = {40, 40, 0.1};
    s.objects.push_back(detector);
    return s;
}
std::vector<std::string> Scene::validate(const Catalog& cat) const {
    std::vector<std::string> out;
    if (sources.empty() || sources.size() > 100 || objects.empty() || objects.size() > 500)
        out.push_back("Нужны источники и тела сцены; предел 100 источников / 500 тел");
    if (rayCount < 1 || rayCount > 5000000 || maxSegments < 1 || maxSegments > 500 ||
        displayRays > 1000)
        out.push_back("Некорректные пределы трассировки");
    for (auto& s : sources) {
        if (!finite(s.pose.position) || !finite(s.pose.tilt) ||
            !finite({s.width, s.height, s.power}) || s.width <= 0 || s.height <= 0 ||
            s.power <= 0 || !std::isfinite(s.wavelength) || s.wavelength < 0.2 ||
            s.wavelength > 5 || !std::isfinite(s.coneAngle) || s.coneAngle <= 0 ||
            s.coneAngle >= 90)
            out.push_back("Некорректный источник: " + s.name);
    }
    bool detector = false;
    for (auto& o : objects) {
        if (o.kind == ObjectKind::Prism && (std::min({o.size.x, o.size.y, o.size.z}) < 1e-5 ||
                                            std::max({o.size.x, o.size.y, o.size.z}) > 1e6))
            out.push_back("Размеры призмы должны быть 0.00001…1000000 мм: " + o.name);
        if ((o.kind == ObjectKind::Sphere || o.kind == ObjectKind::Lens ||
             o.kind == ObjectKind::Cylinder) &&
            std::abs(o.size.x - o.size.y) > 1e-8)
            out.push_back("Круговое тело требует одинаковых X/Y размеров: " + o.name);
        if (o.kind == ObjectKind::Sphere && std::abs(o.size.x - o.size.z) > 1e-8)
            out.push_back("Сфера требует одинаковых X/Y/Z размеров: " + o.name);
        detector |= o.kind == ObjectKind::Detector;
        if (!finite(o.pose.position) || !finite(o.pose.tilt) || !finite(o.size) || o.size.x <= 0 ||
            o.size.y <= 0 || o.size.z <= 0 || !std::isfinite(o.reflectivity) ||
            o.reflectivity < 0 || o.reflectivity > 1 || !std::isfinite(o.radius1) ||
            !std::isfinite(o.radius2))
            out.push_back("Некорректное тело: " + o.name);
        if (o.kind == ObjectKind::Detector && (o.nx < 1 || o.ny < 1 || o.nx > 512 || o.ny > 512))
            out.push_back("Сетка детектора должна быть 1…512");
        if (o.kind == ObjectKind::Lens) {
            Surface a, b;
            a.radius = o.radius1;
            b.radius = o.radius2;
            double h = o.size.x / 2;
            double sf = sag(a, h, 0), sb = sag(b, h, 0);
            if (!std::isfinite(sf) || !std::isfinite(sb) || o.size.z + sb - sf <= 0)
                out.push_back("Края линзы пересекаются: " + o.name);
        }
        try {
            for (auto& s : sources)
                cat.get(o.material).index(s.wavelength);
        } catch (const std::exception& e) {
            out.push_back(e.what());
        }
    }
    if (!detector)
        out.push_back("Нет детектора");
    return out;
}
struct ObjectHit {
    double t = 1e100;
    Vec3 point, normal;
};
static std::optional<ObjectHit> hitObject(const SceneObject& o, const Ray& r) {
    Vec3 p = o.pose.local(r.origin), d = o.pose.inverseRotate(r.direction);
    ObjectHit best;
    auto accept = [&](double t, Vec3 n) {
        if (t > 1e-7 && t < best.t) {
            best = {t, o.pose.world(p + d * t), o.pose.rotate(n.unit())};
        }
    };
    double hx = o.size.x / 2, hy = o.size.y / 2, hz = o.size.z / 2;
    if (o.kind == ObjectKind::Mirror || o.kind == ObjectKind::Detector) {
        if (std::abs(d.z) > 1e-14) {
            double t = -p.z / d.z;
            Vec3 q = p + d * t;
            if (std::abs(q.x) <= hx && std::abs(q.y) <= hy)
                accept(t, {0, 0, 1});
        }
    } else if (o.kind == ObjectKind::Sphere) {
        double B = p.dot(d), C = p.norm2() - hx * hx, disc = B * B - C;
        if (disc >= 0)
            for (double t : {-B - sqrt(disc), -B + sqrt(disc)})
                accept(t, (p + d * t) / hx);
    } else if (o.kind == ObjectKind::Prism) {
        auto planes = prismPlanes(o.size);
        for (auto& plane : planes) {
            double denominator = plane.normal.dot(d);
            if (std::abs(denominator) < 1e-14)
                continue;
            double t = (plane.offset - plane.normal.dot(p)) / denominator;
            Vec3 point = p + d * t;
            bool inside = true;
            for (auto& bound : planes)
                inside &= bound.normal.dot(point) <= bound.offset + 1e-8;
            if (inside)
                accept(t, plane.normal);
        }
    } else if (o.kind == ObjectKind::Box) {
        double a[3] = {p.x, p.y, p.z}, b[3] = {d.x, d.y, d.z}, h[3] = {hx, hy, hz};
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(b[axis]) < 1e-14)
                continue;
            for (double sign : {-1., 1.}) {
                double t = (sign * h[axis] - a[axis]) / b[axis];
                Vec3 q = p + d * t;
                if (std::abs(q.x) <= hx + 1e-8 && std::abs(q.y) <= hy + 1e-8 &&
                    std::abs(q.z) <= hz + 1e-8) {
                    Vec3 n = axis == 0   ? Vec3{sign, 0, 0}
                             : axis == 1 ? Vec3{0, sign, 0}
                                         : Vec3{0, 0, sign};
                    accept(t, n);
                }
            }
        }
    } else if (o.kind == ObjectKind::Cylinder) {
        double A = d.x * d.x + d.y * d.y, B = p.x * d.x + p.y * d.y,
               C = p.x * p.x + p.y * p.y - hx * hx, disc = B * B - A * C;
        if (A > 1e-14 && disc >= 0)
            for (double t : {(-B - sqrt(disc)) / A, (-B + sqrt(disc)) / A}) {
                Vec3 q = p + d * t;
                if (std::abs(q.z) <= hz)
                    accept(t, {q.x, q.y, 0});
            }
        if (std::abs(d.z) > 1e-14)
            for (double sign : {-1., 1.}) {
                double t = (sign * hz - p.z) / d.z;
                Vec3 q = p + d * t;
                if (q.x * q.x + q.y * q.y <= hx * hx)
                    accept(t, {0, 0, sign});
            }
    } else if (o.kind == ObjectKind::Lens) {
        for (int side = 0; side < 2; ++side) {
            Surface surface;
            surface.radius = side ? o.radius2 : o.radius1;
            surface.semiDiameter = hx;
            Pose face = o.pose;
            face.position = o.pose.world({0, 0, side ? hz : -hz});
            auto hit = intersectSurface(surface, face, r);
            if (hit && hit->distance > 1e-7 && hit->distance < best.t)
                best = {hit->distance, hit->point, hit->normal * (side ? 1 : -1)};
        }
        // Cylindrical edge completes the closed solid.
        double A = d.x * d.x + d.y * d.y, B = p.x * d.x + p.y * d.y,
               C = p.x * p.x + p.y * p.y - hx * hx, disc = B * B - A * C;
        if (A > 1e-14 && disc >= 0) {
            Surface a, b;
            a.radius = o.radius1;
            b.radius = o.radius2;
            double low = -hz + sag(a, hx, 0), high = hz + sag(b, hx, 0);
            for (double t : {(-B - sqrt(disc)) / A, (-B + sqrt(disc)) / A}) {
                Vec3 q = p + d * t;
                if (q.z >= low && q.z <= high)
                    accept(t, {q.x, q.y, 0});
            }
        }
    }
    if (best.t == 1e100)
        return {};
    return best;
}
static bool contains(const SceneObject& o, Vec3 world) {
    Vec3 p = o.pose.local(world);
    double r = o.size.x / 2, h = o.size.z / 2;
    if (o.kind == ObjectKind::Sphere)
        return p.norm2() < r * r;
    if (o.kind == ObjectKind::Box)
        return std::abs(p.x) < r && std::abs(p.y) < o.size.y / 2 && std::abs(p.z) < h;
    if (o.kind == ObjectKind::Prism) {
        for (auto& plane : prismPlanes(o.size))
            if (plane.normal.dot(p) >= plane.offset)
                return false;
        return true;
    }
    if (o.kind == ObjectKind::Cylinder)
        return p.x * p.x + p.y * p.y < r * r && std::abs(p.z) < h;
    if (o.kind == ObjectKind::Lens) {
        Surface a, b;
        a.radius = o.radius1;
        b.radius = o.radius2;
        return p.x * p.x + p.y * p.y < r * r && p.z > -h + sag(a, p.x, p.y) &&
               p.z < h + sag(b, p.x, p.y);
    }
    return false;
}
SceneTrace traceScene(const Scene& scene, const Catalog& catalog,
                      std::function<bool(size_t, size_t)> progress) {
    auto errors = scene.validate(catalog);
    if (!errors.empty())
        throw std::invalid_argument(errors.front());
    auto start = std::chrono::steady_clock::now();
    SceneTrace out;
    for (size_t i = 0; i < scene.objects.size(); ++i)
        if (scene.objects[i].kind == ObjectKind::Detector) {
            auto& d = scene.objects[i];
            out.detectors.push_back({i, d.nx, d.ny, d.size.x * d.size.y / (d.nx * d.ny),
                                     std::vector<double>(d.nx * d.ny)});
        }
    std::mt19937_64 rng(scene.seed);
    std::uniform_real_distribution<double> uniform(0, 1);
    auto u = [&]() { return uniform(rng); };
    size_t total = scene.rayCount * scene.sources.size(), done = 0;
    for (size_t sourceIndex = 0; sourceIndex < scene.sources.size(); ++sourceIndex) {
        const auto& source = scene.sources[sourceIndex];
        const size_t displayQuota = scene.displayRays / scene.sources.size() +
                                    (sourceIndex < scene.displayRays % scene.sources.size());
        for (size_t i = 0; i < scene.rayCount; ++i) {
            if (done % 256 == 0 && progress && !progress(done, total)) {
                out.cancelled = true;
                goto finish;
            }
            Ray ray;
            Vec3 local;
            if (source.shape == SourceShape::Rectangle)
                local = {(u() - 0.5) * source.width, (u() - 0.5) * source.height, 0};
            if (source.shape == SourceShape::Ellipse) {
                double angle = 2 * pi * u(), radius = sqrt(u());
                local = {radius * cos(angle) * source.width / 2,
                         radius * sin(angle) * source.height / 2, 0};
            }
            ray.origin = source.pose.world(local);
            double phi = 2 * pi * u(), cost;
            if (source.distribution == Distribution::Isotropic)
                cost = 1 - 2 * u();
            else if (source.distribution == Distribution::Cosine)
                cost = sqrt(u());
            else
                cost = 1 - u() * (1 - cos(source.coneAngle * deg));
            double sint = sqrt(1 - cost * cost);
            ray.direction = source.pose.rotate({sint * cos(phi), sint * sin(phi), cost});
            ray.wavelength = source.wavelength;
            ray.power = source.power / scene.rayCount;
            ++out.launched;
            ++done;
            out.launchedPower += ray.power;
            double currentIndex = 1;
            for (auto& object : scene.objects)
                if (object.interaction == Interaction::Dielectric && contains(object, ray.origin))
                    currentIndex = catalog.get(object.material).index(ray.wavelength);
            bool display = i < displayQuota;
            std::vector<Vec3> path{ray.origin};
            std::vector<unsigned char> pathKinds;
            unsigned char kind = 0;
            bool ended = false;
            for (size_t segment = 0; segment < scene.maxSegments; ++segment) {
                std::optional<ObjectHit> closest;
                size_t objectIndex = 0;
                for (size_t j = 0; j < scene.objects.size(); ++j) {
                    auto h = hitObject(scene.objects[j], ray);
                    if (h && (!closest || h->t < closest->t)) {
                        closest = h;
                        objectIndex = j;
                    }
                }
                if (!closest) {
                    ++out.escaped;
                    out.escapedPower += ray.power;
                    if (display) {
                        path.push_back(ray.origin + ray.direction * 60);
                        pathKinds.push_back(kind);
                    }
                    ended = true;
                    break;
                }
                auto& object = scene.objects[objectIndex];
                Vec3 point = closest->point, normal = closest->normal;
                if (display) {
                    path.push_back(point);
                    pathKinds.push_back(kind);
                }
                if (object.kind == ObjectKind::Detector) {
                    Vec3 q = object.pose.local(point);
                    int ix = std::clamp(int((q.x / object.size.x + 0.5) * object.nx), 0,
                                        object.nx - 1),
                        iy = std::clamp(int((q.y / object.size.y + 0.5) * object.ny), 0,
                                        object.ny - 1);
                    for (auto& detector : out.detectors)
                        if (detector.objectIndex == objectIndex) {
                            detector.watts[iy * object.nx + ix] += ray.power;
                            ++detector.hits;
                        }
                    ++out.detected;
                    out.detectedPower += ray.power;
                    ended = true;
                    break;
                }
                if (object.interaction == Interaction::Absorb) {
                    ++out.absorbed;
                    out.absorbedPower += ray.power;
                    ended = true;
                    break;
                }
                if (object.kind == ObjectKind::Mirror ||
                    object.interaction == Interaction::Reflect ||
                    object.interaction == Interaction::Diffuse) {
                    out.absorbedPower += ray.power * (1 - object.reflectivity);
                    ray.power *= object.reflectivity;
                    if (object.interaction == Interaction::Diffuse) {
                        kind = 2;
                        if (ray.direction.dot(normal) > 0)
                            normal = -normal;
                        Vec3 tangent = (std::abs(normal.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0})
                                           .cross(normal)
                                           .unit(),
                             bitangent = normal.cross(tangent);
                        double rr = sqrt(u()), a = 2 * pi * u();
                        ray.direction = (tangent * (rr * cos(a)) + bitangent * (rr * sin(a)) +
                                         normal * sqrt(1 - rr * rr))
                                            .unit();
                    } else {
                        kind = 1;
                        ray.direction = reflect(ray.direction, normal);
                    }
                    if (ray.power < 1e-20) {
                        ++out.absorbed;
                        ended = true;
                        break;
                    }
                } else {
                    bool entering = ray.direction.dot(normal) < 0;
                    double nextIndex =
                        entering ? catalog.get(object.material).index(ray.wavelength) : 1;
                    Vec3 transmitted;
                    if (!refract(ray.direction, normal, currentIndex, nextIndex, transmitted) ||
                        u() < fresnel(ray.direction, normal, currentIndex, nextIndex)) {
                        ray.direction = reflect(ray.direction, normal);
                        kind = 1;
                    } else {
                        ray.direction = transmitted;
                        currentIndex = nextIndex;
                    }
                }
                ray.origin = point + ray.direction * 1e-6;
            }
            if (!ended) {
                ++out.truncated;
                out.truncatedPower += ray.power;
            }
            if (display) {
                out.paths.push_back(std::move(path));
                out.pathKinds.push_back(std::move(pathKinds));
                out.pathSources.push_back(sourceIndex);
                out.pathWavelengths.push_back(ray.wavelength);
            }
        }
    }
finish:
    // Interleave stored paths so a reduced display limit still represents multiple sources.
    // This reorders presentation data only; no random draws or power tallies change.
    if (scene.sources.size() > 1 && !out.paths.empty()) {
        auto paths = std::move(out.paths);
        auto kinds = std::move(out.pathKinds);
        auto sources = std::move(out.pathSources);
        auto wavelengths = std::move(out.pathWavelengths);
        std::vector<std::vector<size_t>> groups(scene.sources.size());
        size_t maximum = 0;
        for (size_t i = 0; i < sources.size(); ++i) {
            auto& group = groups[sources[i]];
            group.push_back(i);
            maximum = std::max(maximum, group.size());
        }
        out.paths.clear();
        out.pathKinds.clear();
        out.pathSources.clear();
        out.pathWavelengths.clear();
        for (size_t rank = 0; rank < maximum; ++rank)
            for (const auto& group : groups)
                if (rank < group.size()) {
                    size_t i = group[rank];
                    out.paths.push_back(std::move(paths[i]));
                    out.pathKinds.push_back(std::move(kinds[i]));
                    out.pathSources.push_back(sources[i]);
                    out.pathWavelengths.push_back(wavelengths[i]);
                }
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return out;
}
} // namespace optics
