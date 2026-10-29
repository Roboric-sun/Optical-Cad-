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
    auto& operands=p.optimization->operands;
    std::erase_if(operands,[&](const auto& o) {
        if(o.kind!=MeritKind::EdgeThickness)return false;
        const bool deleted=removed && (o.surface<at+removed && o.surface+1>=at);
        const bool split=inserted && at==o.surface+1;
        return deleted || split;
    });
    for(auto& o:operands)if(o.kind==MeritKind::EdgeThickness && o.surface>=at+removed)
        o.surface=o.surface-removed+inserted;
    if (p.optimization->refocus)
        std::erase_if(variables, [&](auto v) {
            return v.parameter == VariableParameter::Defocus ||
                   (v.parameter == VariableParameter::Thickness && v.surface + 1 == p.system.surfaces.size());
        });
    if (variables.empty() || operands.empty()) p.optimization.reset();
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
template <size_t N> static QJsonArray numbers(const std::array<double, N>& a) {
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
    bool indexOffsets=false;
    bool extended = p.system.fieldType == FieldType::RealImageHeight;
    if (p.optimization) for (const auto& variable : p.optimization->variables)
        extended |= int(variable.parameter) > int(VariableParameter::Defocus);
    for (auto& m : p.catalog.materials) {
        QJsonObject row{{"name", QString::fromStdString(m.name)},
                                     {"b", triple(m.b)},
                                     {"c", triple(m.c)},
                                     {"nd", m.nd},
                                     {"vd", m.vd},
                                     {"minWavelength", m.minWavelength},
                                     {"maxWavelength", m.maxWavelength}};
        if (m.schottFormula) { row["schott"] = numbers(m.schott); extended = true; }
        if(m.indexOffset!=0){row["indexOffset"]=m.indexOffset;indexOffsets=true;}
        materials.append(row);
    }
    bool freeform=false;
    for (auto& s : p.system.surfaces) {
        const bool extra = std::any_of(s.asphere.begin() + 4, s.asphere.end(), [](double v) { return v != 0; }) ||
                           std::any_of(s.oddAsphere.begin(), s.oddAsphere.end(), [](double v) { return v != 0; });
        extended |= extra;
        auto coefficients = numbers(s.asphere);
        if (!extra) while (coefficients.size() > 4) coefficients.removeLast();
        QJsonObject row{{"name", QString::fromStdString(s.name)},
                                    {"kind", int(s.kind)},
                                    {"radius", s.radius},
                                    {"thickness", s.thickness},
                                    {"semiDiameter", s.semiDiameter},
                                    {"conic", s.conic},
                                    {"asphere", coefficients},
                                    {"material", QString::fromStdString(s.material)},
                                    {"transmission", s.transmission},
                                    {"reflectivity", s.reflectivity},
                                    {"decenter", vector(s.decenter)},
                                    {"tilt", vector(s.tilt)}};
        if (extra) row["oddAsphere"] = numbers(s.oddAsphere);
        if(s.profile!=SurfaceProfile::Standard || s.aperture.shape!=ApertureShape::Circle || s.aperture.obscuration!=0)
            row["shape"]=QJsonObject{{"profile",int(s.profile)},{"rotationRadius",s.rotationRadius},
                {"aperture",int(s.aperture.shape)},{"halfX",s.aperture.halfX},{"halfY",s.aperture.halfY},{"obscuration",s.aperture.obscuration}};
        if(s.profile==SurfaceProfile::XYPolynomial){auto shape=row["shape"].toObject();shape["xySag"]=numbers(s.xySag);row["shape"]=shape;freeform=true;}
        if(s.coordinates.mode!=CoordinateMode::None) {
            const auto& a=s.coordinates;
            row["coordinates"]=QJsonObject{{"mode",int(a.mode)},{"order",int(a.order)},
                {"shift",vector(a.shift)},{"tilt",vector(a.tilt)},{"reference",a.reference}};
        }
        surfaces.append(row);
    }
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
    if(usesPhysicalStop(s))seq["pupilDefinition"]="physical-stop-diameter";
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
    bool modernOptimization=false;
    if(p.optimization) {
        const auto& plan=*p.optimization;
        modernOptimization=plan.algorithm!=OptimizationAlgorithm::Coordinate || plan.starts!=1 || plan.allFieldsFocus;
        for(const auto& o:plan.operands)modernOptimization|=int(o.kind)>int(MeritKind::Throughput);
    }
    QJsonObject root{{"format", "optical-cad"},
                                     {"version", freeform ? 13 : indexOffsets ? 12 : usesAdvancedSurfaces(s) ? 10 : hasCoordinates(s) ? 9 : modernOptimization ? 8 : extended ? 7 : s.fieldType != FieldType::Angle ? 6 : vignetting ? 5 : marginal ? 4 : curvature ? 3 : s.solves.empty() ? 1 : 2},
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
        for (auto o : plan.operands) {
            QJsonObject row{{"kind",int(o.kind)},{"field",o.field},{"target",o.target},
                            {"scale",o.scale},{"weight",o.weight}};
            if(modernOptimization){row["frequency"]=o.frequency;row["surface"]=double(o.surface);row["height"]=o.height;}
            operands.append(row);
        }
        QJsonObject settings = QJsonObject{{"variables", variables}, {"operands", operands},
                                           {"iterations", double(plan.iterations)},
                                           {"pupilGrid", plan.pupilGrid},
                                           {"minimumThroughput", plan.minimumThroughput},
                                           {"refocus", plan.refocus}};
        if(modernOptimization) {
            settings["algorithm"]=int(plan.algorithm); settings["starts"]=double(plan.starts);
            settings["allFieldsFocus"]=plan.allFieldsFocus;
        }
        root["optimization"]=settings;
    }
    writeStudies(root,p);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
Project deserializeProject(const QByteArray& bytes, bool validate) {
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw std::invalid_argument("Invalid project JSON");
    auto root = doc.object();
    if (root["format"] != "optical-cad" || (root["version"] != 1 && root["version"] != 2 && root["version"] != 3 && root["version"] != 4 && root["version"] != 5 && root["version"] != 6 && root["version"] != 7 && root["version"] != 8 && root["version"] != 9 && root["version"] != 10 && root["version"] != 11 && root["version"] != 12 && root["version"] != 13))
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
        if (j.contains("schott")) {
            const auto coefficients = j["schott"].toArray();
            if (root["version"].toInt() < 7 || coefficients.size() != 6) throw std::invalid_argument("Schott dispersion requires format 7 and six coefficients");
            m.schottFormula = true;
            for (int i=0;i<6;++i) {
                if (!coefficients[i].isDouble()) throw std::invalid_argument("Invalid Schott coefficient");
                m.schott[i] = coefficients[i].toDouble();
            }
        }
        if(j.contains("indexOffset")){if(root["version"].toInt()<12)throw std::invalid_argument("Index offset requires format 12");m.indexOffset=number(j,"indexOffset");if(m.name=="AIR"&&m.indexOffset!=0)throw std::invalid_argument("AIR cannot have an offset; use a separate material");}
        if (m.name != "AIR")
            p.catalog.add(m);
    }
    auto seq = root["sequential"].toObject();
    auto& s = p.system;
    if (seq.contains("fieldType")) {
        if (root["version"].toInt() < 6) throw std::invalid_argument("Field type metadata requires format 6 or newer");
        s.fieldType = FieldType(integer(seq, "fieldType", root["version"].toInt() >= 7 ? 3 : 2));
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
        if(j.contains("coordinates")) {
            if(root["version"].toInt()<9 || !j["coordinates"].isObject())throw std::invalid_argument("Coordinate metadata requires format 9");
            const auto a=j["coordinates"].toObject();auto& change=surf.coordinates;
            change.mode=CoordinateMode(integer(a,"mode",3));change.order=CoordinateOrder(integer(a,"order",1));
            change.shift=readVector(a["shift"]);change.tilt=readVector(a["tilt"]);
            const double reference=number(a,"reference");
            if(reference < -1 || reference >= s.surfaces.size() || std::floor(reference)!=reference)
                throw std::invalid_argument("Coordinate reference must precede its surface");
            change.reference=int(reference);
        }
        if(j.contains("shape")) {
            if(root["version"].toInt()<10 || !j["shape"].isObject())throw std::invalid_argument("Surface shape requires format 10");
            const auto a=j["shape"].toObject();surf.profile=SurfaceProfile(integer(a,"profile",root["version"].toInt()>=13?4:3));surf.rotationRadius=number(a,"rotationRadius");
            if(a.contains("xySag")||surf.profile==SurfaceProfile::XYPolynomial){if(root["version"].toInt()<13||!a["xySag"].isArray()||a["xySag"].toArray().size()!=7)throw std::invalid_argument("XY sag requires format 13 and seven coefficients");const auto coefficients=a["xySag"].toArray();for(int i=0;i<7;++i){if(!coefficients[i].isDouble())throw std::invalid_argument("Invalid XY coefficient");surf.xySag[i]=coefficients[i].toDouble();}}
            surf.aperture.shape=ApertureShape(integer(a,"aperture",2));surf.aperture.halfX=number(a,"halfX");surf.aperture.halfY=number(a,"halfY");surf.aperture.obscuration=number(a,"obscuration");
        }
        auto a = j["asphere"].toArray();
        if (a.size() != 4 && !(root["version"].toInt() >= 7 && a.size() == 10))
            throw std::invalid_argument("Invalid asphere array");
        for (int i = 0; i < a.size(); ++i) {
            if (!a[i].isDouble())
                throw std::invalid_argument("Invalid aspheric coefficient");
            surf.asphere[i] = a[i].toDouble();
        }
        if (j.contains("oddAsphere")) {
            auto odd = j["oddAsphere"].toArray();
            if (root["version"].toInt() < 7 || odd.size() != 10) throw std::invalid_argument("Odd asphere requires format 7 and ten coefficients");
            for (int i = 0; i < 10; ++i) {
                if (!odd[i].isDouble()) throw std::invalid_argument("Invalid odd asphere coefficient");
                surf.oddAsphere[i] = odd[i].toDouble();
            }
        }
        s.surfaces.push_back(surf);
    }
    if(usesPhysicalStop(s) && (root["version"].toInt()<9 || seq["pupilDefinition"]!="physical-stop-diameter"))throw std::invalid_argument("Coordinate projects require explicit physical STOP diameter");
    if(!usesPhysicalStop(s) && seq.contains("pupilDefinition"))throw std::invalid_argument("Unexpected coordinate pupil definition");
    if (seq.contains("solves")) {
        if (root["version"] == 1) throw std::invalid_argument("Solves require project format 2 or newer");
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
        const bool modern=root["version"].toInt()==8 || j.contains("algorithm") || j.contains("starts") || j.contains("allFieldsFocus");
        if(modern && root["version"].toInt()<8)throw std::invalid_argument("New optimizer metadata requires format 8");
        if(modern) {
            plan.algorithm=OptimizationAlgorithm(integer(j,"algorithm",1));
            plan.starts=integer(j,"starts",8);
            if(!j["allFieldsFocus"].isBool())throw std::invalid_argument("Missing allFieldsFocus");
            plan.allFieldsFocus=j["allFieldsFocus"].toBool();
        } else {
            if(j.contains("algorithm") || j.contains("starts") || j.contains("allFieldsFocus"))
                throw std::invalid_argument("New optimizer metadata requires format 8");
        }
        if (!j["variables"].isArray() || !j["operands"].isArray() || !j["refocus"].isBool() ||
            j["variables"].toArray().size() > 64 || j["operands"].toArray().size() > 64)
            throw std::invalid_argument("Invalid optimization table");
        for (auto entry : j["variables"].toArray()) {
            auto v = entry.toObject();
            plan.variables.push_back({VariableParameter(integer(v, "parameter", root["version"].toInt() >= 7 ? int(VariableParameter::A21) : 7)),
                                      integer(v, "surface", 499), number(v, "lower"),
                                      number(v, "upper"), number(v, "step")});
        }
        for (auto entry : j["operands"].toArray()) {
            auto o = entry.toObject();
            double field = number(o, "field");
            if (field < -1 || field > 49 || std::floor(field) != field)
                throw std::invalid_argument("Invalid merit field");
            plan.operands.push_back({MeritKind(integer(o, "kind", modern ? int(MeritKind::EdgeThickness) : 6)), int(field),
                                     number(o, "target"), number(o, "scale"), number(o, "weight")});
            if(modern) {
                auto& operand=plan.operands.back();operand.frequency=number(o,"frequency");
                operand.surface=integer(o,"surface",499);operand.height=number(o,"height");
            } else if(o.contains("frequency") || o.contains("surface") || o.contains("height"))
                throw std::invalid_argument("New merit metadata requires format 8");
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
    readStudies(root,p,validate);
    return p;
}
void saveProject(const QString& path, const Project& p) {
    auto studyErrors=validateConfigurations({p.system,p.catalog},p.configurations);if(studyErrors.empty()&&p.tolerance)studyErrors=p.tolerance->validate({p.system,p.catalog},p.configurations);if(!studyErrors.empty())throw std::invalid_argument(studyErrors.front());
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
