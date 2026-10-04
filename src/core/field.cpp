#include "optics/model.hpp"

namespace optics {
bool validVignetting(const Field& f) {
    for (double v : {f.vux, f.vlx, f.vuy, f.vly})
        if (!std::isfinite(v) || v < 0 || v >= 1) return false;
    return true;
}
bool hasVignetting(const Field& f) {
    return f.vux != 0 || f.vlx != 0 || f.vuy != 0 || f.vly != 0;
}
Vec3 vignettedPupil(const Field& f, double x, double y) {
    if (!validVignetting(f) || !std::isfinite(x) || !std::isfinite(y))
        throw std::invalid_argument("Виньетирование: нужны конечные координаты и коэффициенты 0 ≤ V < 1");
    return {x * (1 - (x < 0 ? f.vlx : f.vux)), y * (1 - (y < 0 ? f.vly : f.vuy)), 0};
}
std::optional<Vec3> nominalPupil(const Field& f, double x, double y) {
    if (!validVignetting(f) || !std::isfinite(x) || !std::isfinite(y))
        throw std::invalid_argument("Виньетирование: нужны конечные координаты и коэффициенты 0 ≤ V < 1");
    Vec3 p{x / (1 - (x < 0 ? f.vlx : f.vux)), y / (1 - (y < 0 ? f.vly : f.vuy)), 0};
    if (p.norm2() > 1 + 1e-12) return {};
    return p;
}
} // namespace optics
