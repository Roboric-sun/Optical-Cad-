#include "project.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

using namespace optics;
void reindexOptimization(Project& p, size_t at, size_t removed, size_t inserted) {
    if (!p.optimization) return;
    auto& variables = p.optimization->variables;
    std::erase_if(variables, [=](auto v) {
        return v.parameter != VariableParameter::Defocus && v.surface >= at &&
               v.surface < at + removed;
    });
    for (auto& v : variables)
        if (v.parameter != VariableParameter::Defocus && v.surface >= at + removed)
            v.surface = v.surface - removed + inserted;
    if (p.optimization->refocus)
        std::erase_if(variables, [&](auto v) {
            return v.parameter == VariableParameter::Defocus ||
                   (v.parameter == VariableParameter::Thickness && v.surface + 1 == p.system.surfaces.size());
        });
    if (variables.empty()) p.optimization.reset();
}
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
    for (auto f : p.system.fields) {
        QJsonArray row{f.x, f.y, f.weight};
        if (hasVignetting(f))
            for (double v : {f.vux, f.vlx, f.vuy, f.vly}) row.append(v);
        fields.append(row);
    }
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
    if (!s.solves.empty()) {
        QJsonArray solves;
        for (const auto& a : s.solves) {
            QJsonObject rule{{"parameter", int(a.parameter)}, {"surface", double(a.surface)},
                {"kind", int(a.kind)}, {"reference", double(a.reference)}, {"scale", a.scale},
                {"offset", a.offset}, {"value", a.value}, {"height", a.height},
                {"first", double(a.first)}, {"last", double(a.last)}};
            if (a.kind == SolveKind::MarginalHeight) {
                rule["field"] = double(a.field);
                rule["wavelength"] = a.wavelength == SIZE_MAX ? -1. : double(a.wavelength);
                rule["pupil"] = a.pupil;
            }
            solves.append(rule);
        }
        seq["solves"] = solves;
    }
    QJsonObject scene{{"name", QString::fromStdString(n.name)},
                      {"objects", objects},
                      {"sources", sources},
                      {"rayCount", double(n.rayCount)},
                      {"displayRays", double(n.displayRays)},
                      {"maxSegments", double(n.maxSegments)},
                      {"seed", QString::number(n.seed)}};
    const bool curvature = std::any_of(s.solves.begin(), s.solves.end(), [](const auto& a) {
        return a.kind == SolveKind::CurvaturePickup;
    });
    const bool marginal = std::any_of(s.solves.begin(), s.solves.end(), [](const auto& a) {
        return a.kind == SolveKind::MarginalHeight;
    });
    if (s.fieldType != FieldType::Angle) seq["fieldType"] = int(s.fieldType);
    const bool vignetting = std::any_of(s.fields.begin(), s.fields.end(), hasVignetting);
    QJsonObject root{{"format", "optical-cad"},
                                     {"version", s.fieldType != FieldType::Angle ? 6 : vignetting ? 5 : marginal ? 4 : curvature ? 3 : s.solves.empty() ? 1 : 2},
                                     {"mode", p.mode},
                                     {"materials", materials},
                                     {"sequential", seq},
                                     {"nonsequential", scene},
                                     {"workspace", p.workspace}};
    if (p.optimization) {
        const auto& plan = *p.optimization;
        QJsonArray variables, operands;
        for (auto v : plan.variables)
            variables.append(QJsonObject{{"parameter", int(v.parameter)},
                                         {"surface", double(v.surface)}, {"lower", v.lower},
                                         {"upper", v.upper}, {"step", v.step}});
        for (auto o : plan.operands)
            operands.append(QJsonObject{{"kind", int(o.kind)}, {"field", o.field},
                                        {"target", o.target}, {"scale", o.scale},
                                        {"weight", o.weight}});
        root["optimization"] = QJsonObject{{"variables", variables}, {"operands", operands},
                                           {"iterations", double(plan.iterations)},
                                           {"pupilGrid", plan.pupilGrid},
                                           {"minimumThroughput", plan.minimumThroughput},
                                           {"refocus", plan.refocus}};
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
Project deserializeProject(const QByteArray& bytes, bool validate) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw std::invalid_argument("Invalid project JSON");
    auto root = doc.object();
    if (root["format"] != "optical-cad" || (root["version"] != 1 && root["version"] != 2 && root["version"] != 3 && root["version"] != 4 && root["version"] != 5 && root["version"] != 6))
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
    if (seq.contains("fieldType")) {
        if (root["version"] != 6) throw std::invalid_argument("Field type metadata requires format 6");
        s.fieldType = FieldType(integer(seq, "fieldType", 2));
    } else if (root["version"] == 6) throw std::invalid_argument("Format 6 requires fieldType");
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
    if (seq.contains("solves")) {
        if (!seq["solves"].isArray() || seq["solves"].toArray().size() > 1000)
            throw std::invalid_argument("Invalid parameter solves table");
        for (auto val : seq["solves"].toArray()) {
            auto j = val.toObject();
            s.solves.push_back({SolveParameter(integer(j, "parameter", 1)),
                integer(j, "surface", 499), SolveKind(integer(j, "kind", root["version"].toInt() >= 4 ? 4 : root["version"] == 3 ? 3 : 2)),
                integer(j, "reference", 500), number(j, "scale"), number(j, "offset"),
                number(j, "value"), number(j, "height"), integer(j, "first", 499),
                integer(j, "last", 500)});
            auto& a = s.solves.back();
            if (a.kind == SolveKind::MarginalHeight) {
                a.field = integer(j, "field", 49);
                const double wave = number(j, "wavelength");
                if (wave < -1 || wave > 19 || std::floor(wave) != wave)
                    throw std::invalid_argument("Invalid ray solve wavelength");
                a.wavelength = wave == -1 ? SIZE_MAX : size_t(wave);
                a.pupil = number(j, "pupil");
            }
        }
    }
    if (!seq["fields"].isArray() || seq["fields"].toArray().size() > 50)
        throw std::invalid_argument("Invalid fields table");
    for (auto val : seq["fields"].toArray()) {
        auto row = val.toArray();
        if (row.size() != 3 && !(root["version"].toInt() >= 5 && row.size() == 7))
            throw std::invalid_argument("Invalid field / vignetting record");
        for (auto v : row) if (!v.isDouble()) throw std::invalid_argument("Invalid field component");
        Field f{row[0].toDouble(), row[1].toDouble(), row[2].toDouble()};
        if (row.size() == 7) {
            f.vux = row[3].toDouble(); f.vlx = row[4].toDouble();
            f.vuy = row[5].toDouble(); f.vly = row[6].toDouble();
        }
        s.fields.push_back(f);
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
    if (validate) applySolves(s, p.catalog);
    if (root.contains("optimization")) {
        if (!root["optimization"].isObject())
            throw std::invalid_argument("Invalid optimization settings");
        auto j = root["optimization"].toObject();
        OptimizationPlan plan;
        if (!j["variables"].isArray() || !j["operands"].isArray() || !j["refocus"].isBool() ||
            j["variables"].toArray().size() > 64 || j["operands"].toArray().size() > 64)
            throw std::invalid_argument("Invalid optimization table");
        for (auto entry : j["variables"].toArray()) {
            auto v = entry.toObject();
            plan.variables.push_back({VariableParameter(integer(v, "parameter", 7)),
                                      integer(v, "surface", 499), number(v, "lower"),
                                      number(v, "upper"), number(v, "step")});
        }
        for (auto entry : j["operands"].toArray()) {
            auto o = entry.toObject();
            double field = number(o, "field");
            if (field < -1 || field > 49 || std::floor(field) != field)
                throw std::invalid_argument("Invalid merit field");
            plan.operands.push_back({MeritKind(integer(o, "kind", 6)), int(field),
                                     number(o, "target"), number(o, "scale"), number(o, "weight")});
        }
        plan.iterations = integer(j, "iterations", 100);
        plan.pupilGrid = int(integer(j, "pupilGrid", 33));
        plan.minimumThroughput = number(j, "minimumThroughput");
        plan.refocus = j["refocus"].toBool();
        // Undo snapshots can temporarily contain settings invalidated by a field edit.
        if (validate) {
            auto errors = plan.validate(s);
            if (!errors.empty()) throw std::invalid_argument(errors.front());
        }
        p.optimization = std::move(plan);
    }
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
    auto bytes = [&] {
        if (p.system.solves.empty()) return serializeProject(p);
        auto resolved = p;
        applySolves(resolved.system, resolved.catalog);
        return serializeProject(resolved);
    }();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save project");
}
Project loadProject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
        throw std::runtime_error("Cannot read project (limit 32 MB)");
    return deserializeProject(file.readAll());
}
