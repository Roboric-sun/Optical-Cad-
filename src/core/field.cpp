#include "optics/model.hpp"

namespace optics {
const char* fieldUnit(FieldType type) { return type == FieldType::Angle ? "°" : "мм"; }
bool validField(const SequentialSystem& s, const Field& f) {
    if (s.fieldType != FieldType::Angle && s.fieldType != FieldType::ObjectHeight &&
        s.fieldType != FieldType::ParaxialImageHeight && s.fieldType != FieldType::RealImageHeight) return false;
    const double limit = s.fieldType == FieldType::Angle ? 80 : 1e8;
    return finite({f.x, f.y, f.weight}) && std::abs(f.x) <= limit && std::abs(f.y) <= limit &&
           f.weight > 0 && validVignetting(f) &&
           (s.fieldType != FieldType::ObjectHeight || s.objectDistance > 0);
}
Field angularField(const SequentialSystem& s, const Catalog& c, Field f) {
    if (!s.solves.empty()) return angularField(resolvedSystem(s, c), c, f);
    if (!validField(s, f) || !std::isfinite(s.objectDistance) || s.objectDistance < 0)
        throw std::invalid_argument("Некорректное поле: высоте объекта требуется конечное расстояние > 0");
    if (s.fieldType == FieldType::Angle) return f;
    if (s.fieldType == FieldType::RealImageHeight) {
        auto angular = s; angular.fieldType = FieldType::Angle;
        auto gaussian = s; gaussian.fieldType = FieldType::ParaxialImageHeight;
        auto result = angularField(gaussian, c, f);
        const double wave = s.wavelengths.at(s.primary).um;
        auto residual = [&](Field trial) {
            auto path = trace(angular, c, pupilRay(angular, c, trial, wave, 0, 0));
            if (path.status != TraceStatus::Complete) throw std::invalid_argument("Реальная высота: главный луч не достигает изображения");
            return path.image - Vec3{f.x, f.y, s.imageZ()};
        };
        for (int i = 0; i < 30; ++i) {
            const auto e = residual(result);
            if (std::hypot(e.x, e.y) < 1e-8) return result;
            const double h = 1e-5;
            auto fx = result, fy = result; fx.x += h; fy.y += h;
            const auto ex = (residual(fx) - e) / h, ey = (residual(fy) - e) / h;
            const double det = ex.x * ey.y - ex.y * ey.x;
            if (std::abs(det) < 1e-14) break;
            const double dx = (ey.y * e.x - ey.x * e.y) / det;
            const double dy = (ex.x * e.y - ex.y * e.x) / det;
            bool improved = false;
            for (double scale = 1; scale >= 1. / 128; scale /= 2) {
                auto trial = result; trial.x -= scale * dx; trial.y -= scale * dy;
                if (!validField(angular, trial)) continue;
                try {
                    auto error = residual(trial);
                    if (std::hypot(error.x, error.y) < std::hypot(e.x, e.y)) {
                        result = trial; improved = true; break;
                    }
                } catch (const std::exception&) {}
            }
            if (!improved) break;
        }
        throw std::invalid_argument("Реальная высота: подбор угла не сошёлся к заданной точке изображения");
    }
    double factor = 1;
    if (s.fieldType == FieldType::ParaxialImageHeight) {
        if (s.primary >= s.wavelengths.size()) throw std::invalid_argument("Первичная волна не существует");
        const auto p = paraxial(s, c, s.wavelengths[s.primary].um);
        // Reduced-angle ABCD has determinant 1 (object medium is AIR).
        // At the Gaussian conjugate h_object = (C L + D) h_image.
        factor = s.objectDistance > 0 ? p.matrix[2] * s.objectDistance + p.matrix[3] : -p.matrix[2];
        if (!std::isfinite(factor) || std::abs(factor) < 1e-12)
            throw std::invalid_argument("Высота изображения не определена: гауссово сопряжение на бесконечности");
    }
    const double slope = s.objectDistance > 0 ? -factor / s.objectDistance : factor;
    f.x = std::atan(f.x * slope) / deg;
    f.y = std::atan(f.y * slope) / deg;
    if (!finite({f.x, f.y, 0}) || std::abs(f.x) > 80 || std::abs(f.y) > 80)
        throw std::invalid_argument("Высота задаёт угол за пределами поддерживаемых ±80°");
    return f;
}
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
