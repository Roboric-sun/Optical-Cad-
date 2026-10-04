#include "project.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>

using namespace optics;
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
    int pupilType = int(value(pupil, "Type"));
    if (pupilType != 0 && pupilType != 1)
        throw std::invalid_argument(
            "Geopter: only entrance pupil diameter and f-number are supported");
    if (value(field, "Type") != 0)
        throw std::invalid_argument("Geopter: only angular object fields supported");
    auto x = field["X"].toArray(), y = field["Y"].toArray(), fw = field["Weight"].toArray();
    if (x.empty() || x.size() > 50 || x.size() != y.size() || x.size() != fw.size())
        throw std::invalid_argument("Geopter: inconsistent fields");
    for (auto key : {"VUX", "VLX", "VUY", "VLY"})
        for (auto v : field[key].toArray())
            if (v.toDouble() != 0)
                throw std::invalid_argument(
                    "Geopter: nonzero vignetting factors are not supported");
    for (int i = 0; i < x.size(); ++i) {
        if (!x[i].isDouble() || !y[i].isDouble() || !fw[i].isDouble())
            throw std::invalid_argument("Geopter: nonnumeric field");
        s.fields.push_back({x[i].toDouble(), y[i].toDouble(), fw[i].toDouble()});
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
        if (type != "SPH" && type != "ASP")
            throw std::invalid_argument("Geopter: unsupported surface type " + type.toStdString());
        double curvature = value(row, "Curvature");
        a.radius = curvature == 0 ? 0 : 1 / curvature;
        a.thickness = value(row, "Thickness");
        if (type == "ASP") {
            a.conic = value(row, "Conic");
            auto coefficients = row["Coefs"].toArray();
            for (int j = 0; j < coefficients.size(); ++j) {
                if (!coefficients[j].isDouble())
                    throw std::invalid_argument("Geopter: invalid aspheric coefficient");
                double v = coefficients[j].toDouble();
                if (j < 4)
                    a.asphere[j] = v;
                else if (v != 0)
                    throw std::invalid_argument("Geopter: aspheric orders above A10 unsupported");
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
    if (pupilType == 1)
        s.pupilDiameter =
            std::abs(paraxial(s, p.catalog, s.wavelengths[s.primary].um).efl) / s.pupilDiameter;
    auto vertices = s.vertices();
    std::vector<double> inferred(s.surfaces.size(), s.pupilDiameter / 2);
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
        if (!explicitAperture[i])
            s.surfaces[i].semiDiameter = inferred[i] * 1.03;
    auto errors = s.validate(p.catalog);
    if (!errors.empty())
        throw std::invalid_argument(errors.front());
    p.workspace["importNotes"] = "Geopter JSON: nm converted to µm; object/image endpoints "
                                 "converted; absent clear apertures inferred from sampled ray "
                                 "envelopes; manual nd:Vd glass uses Cauchy approximation.";
    return p;
}
