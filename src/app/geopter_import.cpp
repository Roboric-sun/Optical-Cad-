#include "project.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

using namespace optics;
static double finitePupilScale(const SequentialSystem& s, const Catalog& c) {
    if (s.objectDistance <= 0) return 1;
    double A=1, B=0, C=0, D=1, n=1;
    const double w=s.wavelengths.at(s.primary).um;
    for(size_t i=0;i<s.stop;++i) {
        const auto& a=s.surfaces.at(i);
        if(a.tilt.norm2() || a.decenter.norm2() || a.kind==SurfaceKind::Mirror)
            throw std::invalid_argument("Geopter EPD conversion requires centered refractors");
        const double next=a.kind==SurfaceKind::Stop?n:c.get(a.material).index(w);
        const double power=a.radius==0?0:(next-n)/a.radius;
        C-=power*A;D-=power*B;n=next;A+=a.thickness/n*C;B+=a.thickness/n*D;
    }
    if(std::abs(A)<1e-12 || std::abs(A+B/s.objectDistance)<1e-12)
        throw std::invalid_argument("Geopter entrance pupil is at a singular conjugate");
    return std::abs(A/(A+B/s.objectDistance));
}
static double value(QJsonObject j, QString key) {
    if (!j[key].isDouble() || !std::isfinite(j[key].toDouble()))
        throw std::invalid_argument(("Geopter: invalid " + key).toStdString());
    return j[key].toDouble();
}
Project importGeopter(const QByteArray& bytes, const Catalog& catalog) {
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        throw std::invalid_argument("Invalid Geopter JSON");
    auto root = doc.object();
    if (root.contains("OpticalCADExtension")) {
        const auto extension = root.take("OpticalCADExtension").toObject();
        if (extension["version"] != 1 || !extension["project"].isObject())
            throw std::invalid_argument("Geopter: invalid Optical CAD extension");
        auto native = deserializeProject(QJsonDocument(extension["project"].toObject()).toJson());
        if (QJsonDocument::fromJson(exportGeopter(native, false)).object() != root)
            throw std::invalid_argument("Geopter: standard geometry differs from native extension; remove the stale extension to import edited geometry");
        return native;
    }
    auto checkSolveMetadata = [](QJsonObject object, QString location) {
        // The pinned Geopter JSON writer has no solve records. Reject unrecognized
        // extensions carrying rules instead of silently importing their cached numbers.
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key().compare("Solve", Qt::CaseInsensitive) == 0 ||
                it.key().compare("Solves", Qt::CaseInsensitive) == 0 ||
                it.key().compare("Pickup", Qt::CaseInsensitive) == 0)
                throw std::invalid_argument(("Geopter: unsupported solve metadata at " + location + "." + it.key()).toStdString());
    };
    checkSolveMetadata(root, "root");
    Project p;
    p.catalog = catalog;
    auto& s = p.system;
    s.name = root["Title"].toString("Импорт Geopter").toStdString();
    if (s.name.empty())
        s.name = "Импорт Geopter";
    s.surfaces.clear();
    s.fields.clear();
    s.wavelengths.clear();
    s.primary = 0;
    auto spec = root["Spec"].toObject(), pupil = spec["Pupil"].toObject(),
         field = spec["Field"].toObject(), wave = spec["Wvl"].toObject(),
         assembly = root["Assembly"].toObject();
    checkSolveMetadata(assembly, "Assembly");
    for (auto it = assembly.begin(); it != assembly.end(); ++it)
        if (it.value().isObject()) checkSolveMetadata(it.value().toObject(), "Assembly." + it.key());
    const double pupilTypeValue = value(pupil, "Type");
    if (pupilTypeValue < 0 || pupilTypeValue > 3 || std::floor(pupilTypeValue) != pupilTypeValue)
        throw std::invalid_argument(
            "Geopter: invalid pupil type (expected EPD/FNO/NAO/NA)");
    const int pupilType = int(pupilTypeValue);
    const double fieldType = value(field, "Type");
    if (fieldType < 0 || fieldType > 2 || std::floor(fieldType) != fieldType)
        throw std::invalid_argument("Geopter: unsupported field type");
    s.fieldType = FieldType(int(fieldType));
    auto x = field["X"].toArray(), y = field["Y"].toArray(), fw = field["Weight"].toArray();
    if (x.empty() || x.size() > 50 || x.size() != y.size() || x.size() != fw.size())
        throw std::invalid_argument("Geopter: inconsistent fields");
    std::array<QJsonArray, 4> vignette;
    const char* names[] = {"VUX", "VLX", "VUY", "VLY"};
    for (size_t j = 0; j < 4; ++j)
        if (field.contains(names[j])) {
            if (!field[names[j]].isArray() || field[names[j]].toArray().size() != x.size())
                throw std::invalid_argument(std::string("Geopter: inconsistent vignetting ") + names[j]);
            vignette[j] = field[names[j]].toArray();
            for (auto v : vignette[j])
                if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0 || v.toDouble() >= 1)
                    throw std::invalid_argument(std::string("Geopter: vignetting must be 0 <= V < 1: ") + names[j]);
        }
    for (int i = 0; i < x.size(); ++i) {
        if (!x[i].isDouble() || !y[i].isDouble() || !fw[i].isDouble())
            throw std::invalid_argument("Geopter: nonnumeric field");
        Field f{x[i].toDouble(), y[i].toDouble(), fw[i].toDouble()};
        f.vux = vignette[0].empty() ? 0 : vignette[0][i].toDouble();
        f.vlx = vignette[1].empty() ? 0 : vignette[1][i].toDouble();
        f.vuy = vignette[2].empty() ? 0 : vignette[2][i].toDouble();
        f.vly = vignette[3].empty() ? 0 : vignette[3][i].toDouble();
        s.fields.push_back(f);
    }
    auto wavelengths = wave["Value"].toArray(), weights = wave["Weight"].toArray();
    if (wavelengths.empty() || wavelengths.size() > 20 || wavelengths.size() != weights.size())
        throw std::invalid_argument("Geopter: inconsistent wavelengths");
    for (int i = 0; i < wavelengths.size(); ++i) {
        if (!wavelengths[i].isDouble() || !weights[i].isDouble())
            throw std::invalid_argument("Geopter: nonnumeric wavelength");
        s.wavelengths.push_back({wavelengths[i].toDouble() / 1000, weights[i].toDouble()});
    }
    double primary = value(wave, "RefIndex");
    if (primary < 0 || primary >= wavelengths.size() || floor(primary) != primary)
        throw std::invalid_argument("Geopter: invalid reference wavelength");
    s.primary = size_t(primary);
    std::vector<int> keys;
    for (auto it = assembly.begin(); it != assembly.end(); ++it) {
        bool ok;
        int index = it.key().toInt(&ok);
        if (ok)
            keys.push_back(index);
    }
    std::sort(keys.begin(), keys.end());
    if (keys.size() < 3 || keys.size() > 502)
        throw std::invalid_argument("Geopter: invalid surface count");
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] != int(i))
            throw std::invalid_argument("Geopter: surface indices must be contiguous");
    auto object = assembly["0"].toObject();
    if (value(object, "Curvature") != 0 || object["Material"] != "AIR")
        throw std::invalid_argument("Geopter: unsupported object surface");
    double distance = value(object, "Thickness");
    s.objectDistance = distance >= 1e7 ? 0 : distance;
    auto image = assembly[QString::number(keys.back())].toObject();
    if (value(image, "Curvature") != 0)
        throw std::invalid_argument("Geopter: curved image surface unsupported");
    double stop = value(assembly, "Stop");
    if (stop < 1 || stop >= keys.back() || floor(stop) != stop)
        throw std::invalid_argument("Geopter: stop must be a physical surface");
    s.stop = size_t(stop - 1);
    s.pupilDiameter = value(pupil, "Value");
    std::vector<bool> explicitAperture;
    for (int i = 1; i < keys.back(); ++i) {
        auto row = assembly[QString::number(i)].toObject();
        Surface a;
        a.name = row["Label"].toString().toStdString();
        if (a.name.empty())
            a.name = "Поверхность " + std::to_string(i);
        QString type = row["Type"].toString();
        if (type != "SPH" && type != "ASP" && type != "ODD")
            throw std::invalid_argument("Geopter: unsupported surface type " + type.toStdString());
        double curvature = value(row, "Curvature");
        a.radius = curvature == 0 ? 0 : 1 / curvature;
        a.thickness = value(row, "Thickness");
        if (type == "ASP" || type == "ODD") {
            a.conic = value(row, "Conic");
            if (!row["Coefs"].isArray()) throw std::invalid_argument("Geopter: missing aspheric coefficients");
            auto coefficients = row["Coefs"].toArray();
            for (int j = 0; j < coefficients.size(); ++j) {
                if (!coefficients[j].isDouble())
                    throw std::invalid_argument("Geopter: invalid aspheric coefficient");
                double v = coefficients[j].toDouble();
                const int power = type == "ASP" ? 4 + 2 * j : 3 + j;
                if (power > 22) {
                    if (v != 0) throw std::invalid_argument("Geopter: aspheric order above A22 unsupported");
                } else if (power % 2) a.oddAsphere[(power - 3) / 2] = v;
                else a.asphere[(power - 4) / 2] = v;
            }
        }
        QString name = row["Material"].toString();
        auto manual = name.split(':');
        if (manual.size() == 2) {
            bool ok1, ok2;
            Material m;
            m.name = "GEOPTER_" + name.toStdString();
            m.nd = manual[0].toDouble(&ok1);
            m.vd = manual[1].toDouble(&ok2);
            if (!ok1 || !ok2)
                throw std::invalid_argument("Geopter: invalid nd:Vd glass");
            p.catalog.add(m);
            a.material = m.name;
        } else {
            name.remove(QRegularExpression("_SCHOTT$", QRegularExpression::CaseInsensitiveOption));
            if (name == "BK7")
                name = "N-BK7";
            a.material = name.toStdString();
            p.catalog.get(a.material);
        }
        auto ap = row["Aperture"].toObject();
        bool fixed = !ap.empty();
        if (fixed) {
            if (ap["Type"] != "Circular")
                throw std::invalid_argument("Geopter: noncircular aperture unsupported");
            a.semiDiameter = value(ap, "Radius");
        } else
            a.semiDiameter = 1;
        explicitAperture.push_back(fixed);
        s.surfaces.push_back(a);
    }
    if(pupilType==0) s.pupilDiameter *= finitePupilScale(s,p.catalog);
    else if(pupilType==1) {
        const auto matrix=paraxial(s,p.catalog,s.wavelengths[s.primary].um).matrix;
        const double power=std::abs(matrix[2]+(s.objectDistance>0?matrix[3]/s.objectDistance:0));
        if(power<1e-12 || s.pupilDiameter<=0) throw std::invalid_argument("Geopter: undefined working f-number");
        s.pupilDiameter=1/(s.pupilDiameter*power);
    } else s.pupilDiameter = apertureDiameter(s, p.catalog, ApertureType(pupilType), s.pupilDiameter);
    auto vertices = s.vertices();
    // Entrance pupil radius is not the clear radius of each internal surface.
    std::vector<double> inferred(s.surfaces.size(), 0);
    for (auto f : s.fields)
        for (auto w : s.wavelengths)
            for (int j = 0; j < 17; ++j) {
                double angle = 2 * pi * j / 16;
                auto ray = pupilRay(s, p.catalog, f, w.um, j == 16 ? 0 : cos(angle),
                                    j == 16 ? 0 : sin(angle));
                auto result = trace(s, p.catalog, ray, false, SIZE_MAX, false);
                for (size_t i = 0; i < s.surfaces.size() && i + 1 < result.points.size(); ++i)
                    inferred[i] = std::max(
                        inferred[i], std::hypot(result.points[i + 1].x, result.points[i + 1].y));
            }
    for (size_t i = 0; i < s.surfaces.size(); ++i)
        if (!explicitAperture[i]) {
            if (!std::isfinite(inferred[i]) || inferred[i] <= 0)
                throw std::invalid_argument("Geopter: no sampled ray reaches surface " + std::to_string(i + 1));
            auto& surface = s.surfaces[i];
            surface.semiDiameter = inferred[i] * 1.03;
            if (surface.radius != 0 && surface.conic > -1)
                surface.semiDiameter = std::min(surface.semiDiameter,
                    std::nextafter(std::abs(surface.radius) / std::sqrt(1 + surface.conic), 0.));
        }
    auto errors = s.validate(p.catalog);
    if (!errors.empty())
        throw std::invalid_argument(errors.front());
    p.workspace["importNotes"] = "Geopter JSON: nm converted to µm; object/image endpoints "
                                 "converted; absent clear apertures inferred from sampled ray "
                                 "envelopes; manual nd:Vd glass uses Cauchy approximation.";
    return p;
}
QByteArray exportGeopter(const Project& project, bool preserveNative) {
    const auto s = resolvedSystem(project.system, project.catalog);
    const auto errors = s.validate(project.catalog);
    if (!errors.empty()) throw std::invalid_argument(errors.front());
    if (s.fieldType == FieldType::RealImageHeight)
        throw std::invalid_argument("Geopter does not define real image-height fields");
    if(s.objectDistance>=1e7) throw std::invalid_argument("Geopter export: finite object distance conflicts with infinity convention");
    QJsonArray x, y, weight, vux, vlx, vuy, vly, waves, weights, fieldColors, waveColors;
    for (const auto& f : s.fields) {
        x.append(f.x); y.append(f.y); weight.append(f.weight);
        vux.append(f.vux); vlx.append(f.vlx); vuy.append(f.vuy); vly.append(f.vly);
        fieldColors.append(QJsonArray{0,0,0,1});
    }
    for (const auto& w : s.wavelengths) { waves.append(w.um * 1000); weights.append(w.weight); waveColors.append(QJsonArray{0,0,0,1}); }
    QJsonObject spec{{"Pupil", QJsonObject{{"Type", 0}, {"Value", s.pupilDiameter / finitePupilScale(s,project.catalog)}}},
        {"Field", QJsonObject{{"Type", int(s.fieldType)}, {"X", x}, {"Y", y}, {"Weight", weight},
                             {"VUX", vux}, {"VLX", vlx}, {"VUY", vuy}, {"VLY", vly}, {"Color",fieldColors}}},
        {"Wvl", QJsonObject{{"RefIndex", double(s.primary)}, {"Value", waves}, {"Weight", weights}, {"Color",waveColors}}}};
    QJsonObject assembly{{"Stop", double(s.stop + 1)},
        {"0", QJsonObject{{"Type", "SPH"}, {"Label", "Object"}, {"Curvature", 0},
                          {"Thickness", s.objectDistance > 0 ? s.objectDistance : 1e10}, {"Material", "AIR"}}}};
    for (size_t i = 0; i < s.surfaces.size(); ++i) {
        const auto& a = s.surfaces[i];
        if (a.kind == SurfaceKind::Mirror || a.tilt.norm2() || a.decenter.norm2() || a.transmission != 1 || a.reflectivity != 1)
            throw std::invalid_argument("Geopter export: mirror, pose or coating is not representable");
        const auto& glass = project.catalog.get(a.material);
        QString material = QString::fromStdString(a.material);
        if (a.kind == SurfaceKind::Stop && i > 0) material = assembly[QString::number(i)].toObject()["Material"].toString();
        else if (a.kind == SurfaceKind::Stop) material = "AIR";
        else if (material != "AIR") {
            const Catalog standard;
            bool builtIn = false;
            try {
                const auto& original = standard.get(a.material);
                builtIn = original.b == glass.b && original.c == glass.c && original.nd == glass.nd && original.vd == glass.vd && original.schottFormula == glass.schottFormula && original.schott == glass.schott;
            } catch (const std::exception&) {}
            if (!builtIn) {
                if (glass.schottFormula || glass.b != std::array<double,3>{})
                    throw std::invalid_argument("Geopter export: custom dispersive glass needs a shared catalog");
                material = QString::number(glass.nd, 'g', 17) + ":" + QString::number(glass.vd, 'g', 17);
            }
        }
        QJsonObject row{{"Type", "SPH"}, {"Label", QString::fromStdString(a.name)},
            {"Curvature", a.radius == 0 ? 0 : 1 / a.radius},
            {"Thickness", a.thickness + (i + 1 == s.surfaces.size() ? s.defocus : 0)},
            {"Material", material}, {"Aperture", QJsonObject{{"Type", "Circular"}, {"Radius", a.semiDiameter}}}};
        const bool odd = std::any_of(a.oddAsphere.begin(), a.oddAsphere.end(), [](double v) { return v != 0; });
        const bool even = std::any_of(a.asphere.begin(), a.asphere.end(), [](double v) { return v != 0; });
        if (odd || even || a.conic != 0) {
            row["Type"] = odd ? "ODD" : "ASP"; row["Conic"] = a.conic;
            QJsonArray coefficients;
            if (odd) {
                if (std::any_of(a.asphere.begin()+5,a.asphere.end(),[](double v){return v!=0;}) ||
                    std::any_of(a.oddAsphere.begin()+5,a.oddAsphere.end(),[](double v){return v!=0;}))
                    throw std::invalid_argument("Geopter ODD supports A3 through A12 only");
                for (int power=3;power<=12;++power)
                    coefficients.append(power%2 ? a.oddAsphere[(power-3)/2] : a.asphere[(power-4)/2]);
            } else for(double v:a.asphere) coefficients.append(v);
            row["Coefs"] = coefficients;
        }
        assembly[QString::number(i+1)] = row;
    }
    assembly[QString::number(s.surfaces.size()+1)] = QJsonObject{{"Type","SPH"},{"Label","Image"},
        {"Curvature",0},{"Thickness",0},{"Material","AIR"}};
    QJsonObject root{{"Title",QString::fromStdString(s.name)}, {"Note","Exported by Optical CAD; native constraints require OpticalCADExtension"}, {"Spec",spec}, {"Assembly",assembly}};
    if (preserveNative) root["OpticalCADExtension"] = QJsonObject{{"version",1},
        {"project",QJsonDocument::fromJson(serializeProject(project)).object()}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
