#include "optics/model.hpp"
#include <sstream>

namespace optics {
double Material::index(double w) const {
    if (!std::isfinite(w) || w < 0.2 || w > 5)
        throw std::invalid_argument("Wavelength must be 0.2–5 µm");
    if (w < minWavelength || w > maxWavelength)
        throw std::invalid_argument("Wavelength outside catalog range: " + name);
    if (name == "AIR")
        return 1;
    if (b[0] != 0 || b[1] != 0 || b[2] != 0) {
        double n2 = 1, w2 = w * w;
        for (size_t i = 0; i < 3; ++i) {
            if (std::abs(w2 - c[i]) < 1e-12)
                throw std::invalid_argument("Sellmeier pole");
            n2 += b[i] * w2 / (w2 - c[i]);
        }
        if (n2 <= 0 || !std::isfinite(n2))
            throw std::invalid_argument("Invalid Sellmeier index");
        return sqrt(n2);
    }
    if (nd <= 0 || !std::isfinite(nd) || vd < 0 || !std::isfinite(vd))
        throw std::invalid_argument("Invalid material");
    if (vd == 0)
        return nd;
    const double f = 0.4861327, d = 0.5875618, cw = 0.6562725;
    const double B = (nd - 1) / vd / (1 / (f * f) - 1 / (cw * cw));
    return nd + B * (1 / (w * w) - 1 / (d * d));
}
Catalog::Catalog() {
    materials = {Material{},
                 {"FUSED_SILICA",
                  {0.6961663, 0.4079426, 0.8974794},
                  {0.00467914825849, 0.01351206307396, 97.9340025379},
                  1.45846,
                  67.82,
                  0.21,
                  3.71}};
#include "schott_data.inc"
}
const Material& Catalog::get(const std::string& name) const {
    for (auto& m : materials)
        if (m.name == name)
            return m;
    throw std::invalid_argument("Unknown glass: " + name);
}
void Catalog::add(Material m) {
    if (m.name.empty() || m.name == "AIR")
        throw std::invalid_argument("Reserved or empty material name");
    if (!std::isfinite(m.nd) || m.nd <= 0 || !std::isfinite(m.vd) || m.vd < 0)
        throw std::invalid_argument("Invalid nd / Vd");
    if (!std::isfinite(m.minWavelength) || !std::isfinite(m.maxWavelength) ||
        m.minWavelength < .2 || m.maxWavelength > 5 || m.minWavelength > m.maxWavelength)
        throw std::invalid_argument("Invalid material wavelength range");
    for (double x : m.b)
        if (!std::isfinite(x))
            throw std::invalid_argument("Invalid Sellmeier coefficient");
    for (double x : m.c)
        if (!std::isfinite(x))
            throw std::invalid_argument("Invalid Sellmeier coefficient");
    for (double w : {m.minWavelength, (m.minWavelength + m.maxWavelength) / 2, m.maxWavelength})
        m.index(w);
    for (auto& v : materials)
        if (v.name == m.name) {
            v = m;
            return;
        }
    materials.push_back(m);
}
void Catalog::importAGF(const std::string& text) {
    Catalog staged = *this;
    std::istringstream in(text);
    std::string line;
    Material m;
    int formula = 0;
    bool pending = false, coefficients = false;
    size_t count = 0;
    auto finish = [&] {
        if (!pending)
            return;
        if (!coefficients)
            throw std::invalid_argument("Missing AGF CD record");
        staged.add(m);
        ++count;
        pending = false;
    };
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag == "NM") {
            finish();
            m = Material{};
            double unused;
            if (!(row >> m.name >> formula >> unused >> m.nd >> m.vd))
                throw std::invalid_argument("Invalid AGF NM record");
            pending = true;
            coefficients = false;
        } else if (tag == "CD" && pending) {
            if (formula != 2)
                throw std::invalid_argument("Only Zemax AGF formula 2 (Sellmeier 1) is supported");
            if (!(row >> m.b[0] >> m.c[0] >> m.b[1] >> m.c[1] >> m.b[2] >> m.c[2]))
                throw std::invalid_argument("Invalid AGF CD record");
            coefficients = true;
        } else if (tag == "LD" && pending) {
            if (!(row >> m.minWavelength >> m.maxWavelength))
                throw std::invalid_argument("Invalid AGF LD record");
        }
    }
    finish();
    if (!count)
        throw std::invalid_argument("No complete supported AGF material records");
    *this = staged;
}
} // namespace optics
