#include "project.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

using namespace optics;
static QJsonArray vector(Vec3 v) {
    return {v.x, v.y, v.z};
}
static Vec3 readVector(QJsonValue v) {
    auto a = v.toArray();
    if (a.size() != 3)
        throw std::invalid_argument("Expected a three component vector");
    for (auto x : a)
        if (!x.isDouble())
            throw std::invalid_argument("Invalid vector component");
    return {a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
}
static QJsonArray numbers(const std::array<double, 4>& a) {
    QJsonArray j;
    for (double v : a)
        j.append(v);
    return j;
}
static QJsonArray triple(const std::array<double, 3>& a) {
    return {a[0], a[1], a[2]};
}
static double number(QJsonObject o, const char* key) {
    if (!o[key].isDouble())
        throw std::invalid_argument(std::string("Missing numeric field: ") + key);
    return o[key].toDouble();
}
static size_t integer(QJsonObject o, const char* key, size_t maximum) {
    double n = number(o, key);
    if (n < 0 || n > double(maximum) || std::floor(n) != n)
        throw std::invalid_argument(std::string("Invalid integer: ") + key);
    return size_t(n);
}
static std::string string(QJsonObject o, const char* key) {
    if (!o[key].isString())
        throw std::invalid_argument(std::string("Missing string field: ") + key);
    return o[key].toString().toStdString();
}
QByteArray serializeProject(const Project& p) {
    QJsonArray materials, surfaces, fields, waves, objects, sources;
    for (auto& m : p.catalog.materials)
        materials.append(QJsonObject{{"name", QString::fromStdString(m.name)},
                                     {"b", triple(m.b)},
                                     {"c", triple(m.c)},
                                     {"nd", m.nd},
                                     {"vd", m.vd},
                                     {"minWavelength", m.minWavelength},
                                     {"maxWavelength", m.maxWavelength}});
    for (auto& s : p.system.surfaces)
        surfaces.append(QJsonObject{{"name", QString::fromStdString(s.name)},
                                    {"kind", int(s.kind)},
                                    {"radius", s.radius},
                                    {"thickness", s.thickness},
                                    {"semiDiameter", s.semiDiameter},
                                    {"conic", s.conic},
                                    {"asphere", numbers(s.asphere)},
                                    {"material", QString::fromStdString(s.material)},
                                    {"transmission", s.transmission},
                                    {"reflectivity", s.reflectivity},
                                    {"decenter", vector(s.decenter)},
                                    {"tilt", vector(s.tilt)}});
    for (auto f : p.system.fields)
        fields.append(QJsonArray{f.x, f.y, f.weight});
    for (auto w : p.system.wavelengths)
        waves.append(QJsonArray{w.um, w.weight});
    for (auto& o : p.scene.objects)
        objects.append(QJsonObject{{"name", QString::fromStdString(o.name)},
                                   {"kind", int(o.kind)},
                                   {"interaction", int(o.interaction)},
                                   {"position", vector(o.pose.position)},
                                   {"tilt", vector(o.pose.tilt)},
                                   {"size", vector(o.size)},
                                   {"radius1", o.radius1},
                                   {"radius2", o.radius2},
                                   {"material", QString::fromStdString(o.material)},
                                   {"reflectivity", o.reflectivity},
                                   {"nx", o.nx},
                                   {"ny", o.ny}});
    for (auto& s : p.scene.sources)
        sources.append(QJsonObject{{"name", QString::fromStdString(s.name)},
                                   {"shape", int(s.shape)},
                                   {"distribution", int(s.distribution)},
                                   {"position", vector(s.pose.position)},
                                   {"tilt", vector(s.pose.tilt)},
                                   {"width", s.width},
                                   {"height", s.height},
                                   {"coneAngle", s.coneAngle},
                                   {"power", s.power},
                                   {"wavelength", s.wavelength}});
    auto& s = p.system;
    auto& n = p.scene;
    QJsonObject seq{{"name", QString::fromStdString(s.name)},
                    {"surfaces", surfaces},
                    {"fields", fields},
                    {"wavelengths", waves},
                    {"primary", double(s.primary)},
                    {"stop", double(s.stop)},
                    {"pupilDiameter", s.pupilDiameter},
                    {"objectDistance", s.objectDistance},
                    {"defocus", s.defocus},
                    {"pupilGrid", s.pupilGrid}};
    QJsonObject scene{{"name", QString::fromStdString(n.name)},
                      {"objects", objects},
                      {"sources", sources},
                      {"rayCount", double(n.rayCount)},
                      {"displayRays", double(n.displayRays)},
                      {"maxSegments", double(n.maxSegments)},
                      {"seed", QString::number(n.seed)}};
    return QJsonDocument(QJsonObject{{"format", "optical-cad"},
                                     {"version", 1},
                                     {"mode", p.mode},
                                     {"materials", materials},
                                     {"sequential", seq},
                                     {"nonsequential", scene},
                                     {"workspace", p.workspace}})
        .toJson(QJsonDocument::Indented);
}
Project deserializeProject(const QByteArray& bytes, bool validate) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw std::invalid_argument("Invalid project JSON");
    auto root = doc.object();
    if (root["format"] != "optical-cad" || root["version"] != 1)
        throw std::invalid_argument("Unsupported project format / version");
    Project p;
    p.mode = int(integer(root, "mode", 1));
    p.workspace = root["workspace"].toObject();
    auto mats = root["materials"].toArray();
    if (mats.size() > 10000)
        throw std::invalid_argument("Material catalog too large");
    for (auto val : mats) {
        auto j = val.toObject();
        Material m;
        m.name = string(j, "name");
        auto b = readVector(j["b"]), c = readVector(j["c"]);
        m.b = {b.x, b.y, b.z};
        m.c = {c.x, c.y, c.z};
        m.nd = number(j, "nd");
        m.vd = number(j, "vd");
        m.minWavelength = j["minWavelength"].toDouble(.2);
        m.maxWavelength = j["maxWavelength"].toDouble(5);
        if (m.name != "AIR")
            p.catalog.add(m);
    }
    auto seq = root["sequential"].toObject();
    auto& s = p.system;
    s.name = string(seq, "name");
    s.surfaces.clear();
    s.fields.clear();
    s.wavelengths.clear();
    if (seq["surfaces"].toArray().size() > 500)
        throw std::invalid_argument("Too many surfaces");
    for (auto val : seq["surfaces"].toArray()) {
        auto j = val.toObject();
        Surface surf;
        surf.name = string(j, "name");
        surf.kind = SurfaceKind(integer(j, "kind", 2));
        surf.radius = number(j, "radius");
        surf.thickness = number(j, "thickness");
        surf.semiDiameter = number(j, "semiDiameter");
        surf.conic = number(j, "conic");
        surf.material = string(j, "material");
        surf.transmission = number(j, "transmission");
        surf.reflectivity = number(j, "reflectivity");
        surf.decenter = readVector(j["decenter"]);
        surf.tilt = readVector(j["tilt"]);
        auto a = j["asphere"].toArray();
        if (a.size() != 4)
            throw std::invalid_argument("Invalid asphere array");
        for (int i = 0; i < 4; ++i) {
            if (!a[i].isDouble())
                throw std::invalid_argument("Invalid aspheric coefficient");
            surf.asphere[i] = a[i].toDouble();
        }
        s.surfaces.push_back(surf);
    }
    for (auto val : seq["fields"].toArray()) {
        auto v = readVector(val);
        s.fields.push_back({v.x, v.y, v.z});
    }
    for (auto val : seq["wavelengths"].toArray()) {
        auto a = val.toArray();
        if (a.size() != 2 || !a[0].isDouble() || !a[1].isDouble())
            throw std::invalid_argument("Invalid wavelength");
        s.wavelengths.push_back({a[0].toDouble(), a[1].toDouble()});
    }
    s.primary = integer(seq, "primary", 20);
    s.stop = integer(seq, "stop", 500);
    s.pupilDiameter = number(seq, "pupilDiameter");
    s.objectDistance = number(seq, "objectDistance");
    s.defocus = number(seq, "defocus");
    s.pupilGrid = int(integer(seq, "pupilGrid", 65));
    auto ns = root["nonsequential"].toObject();
    auto& scene = p.scene;
    scene.name = string(ns, "name");
    scene.objects.clear();
    scene.sources.clear();
    if (ns["objects"].toArray().size() > 500 || ns["sources"].toArray().size() > 100)
        throw std::invalid_argument("Scene too large");
    for (auto val : ns["objects"].toArray()) {
        auto j = val.toObject();
        SceneObject o;
        o.name = string(j, "name");
        o.kind = ObjectKind(integer(j, "kind", 6));
        o.interaction = Interaction(integer(j, "interaction", 3));
        o.pose.position = readVector(j["position"]);
        o.pose.tilt = readVector(j["tilt"]);
        o.size = readVector(j["size"]);
        o.radius1 = number(j, "radius1");
        o.radius2 = number(j, "radius2");
        o.material = string(j, "material");
        o.reflectivity = number(j, "reflectivity");
        o.nx = int(integer(j, "nx", 512));
        o.ny = int(integer(j, "ny", 512));
        scene.objects.push_back(o);
    }
    for (auto val : ns["sources"].toArray()) {
        auto j = val.toObject();
        Source src;
        src.name = string(j, "name");
        src.shape = SourceShape(integer(j, "shape", 2));
        src.distribution = Distribution(integer(j, "distribution", 2));
        src.pose.position = readVector(j["position"]);
        src.pose.tilt = readVector(j["tilt"]);
        src.width = number(j, "width");
        src.height = number(j, "height");
        src.coneAngle = number(j, "coneAngle");
        src.power = number(j, "power");
        src.wavelength = number(j, "wavelength");
        scene.sources.push_back(src);
    }
    scene.rayCount = integer(ns, "rayCount", 5000000);
    scene.displayRays = integer(ns, "displayRays", 1000);
    scene.maxSegments = integer(ns, "maxSegments", 500);
    bool seedOK;
    scene.seed = ns["seed"].toString().toULongLong(&seedOK);
    if (!seedOK)
        throw std::invalid_argument("Invalid random seed");
    if (validate) {
        auto a = s.validate(p.catalog), b = scene.validate(p.catalog);
        if (!a.empty())
            throw std::invalid_argument(a.front());
        if (!b.empty())
            throw std::invalid_argument(b.front());
    }
    return p;
}
void saveProject(const QString& path, const Project& p) {
    QSaveFile file(path);
    auto bytes = serializeProject(p);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save project");
}
Project loadProject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
        throw std::runtime_error("Cannot read project (limit 32 MB)");
    return deserializeProject(file.readAll());
}
