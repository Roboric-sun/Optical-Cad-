#include "optics/model.hpp"
#include <algorithm>
#include <limits>
#include <numeric>

namespace optics {
DetectorStatistics detectorStatistics(const DetectorData& d, double width, double height) {
    if (d.nx < 1 || d.ny < 1 || d.nx > 512 || d.ny > 512 ||
        d.watts.size() != size_t(d.nx) * size_t(d.ny) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0 || height <= 0 || !std::isfinite(d.cellArea) ||
        d.cellArea <= 0 || !std::isfinite(width * height) ||
        std::abs(d.cellArea - width / d.nx * height / d.ny) >
                              1e-9 * std::max(d.cellArea, width / d.nx * height / d.ny))
        throw std::invalid_argument("Некорректная сетка или размеры детектора");
    DetectorStatistics out;
    out.minimum = std::numeric_limits<double>::infinity();
    const double dx = width / d.nx, dy = height / d.ny;
    out.marginalX.resize(d.nx);
    out.marginalY.resize(d.ny);
    for (int x = 0; x < d.nx; ++x) out.x.push_back((x + .5) * dx - width / 2);
    for (int y = 0; y < d.ny; ++y) out.y.push_back((y + .5) * dy - height / 2);
    for (int y = 0; y < d.ny; ++y)
        for (int x = 0; x < d.nx; ++x) {
            double w = d.watts[y * d.nx + x];
            if (!std::isfinite(w) || w < 0)
                throw std::invalid_argument("Некорректная мощность в ячейке детектора");
            out.power += w;
            out.minimum = std::min(out.minimum, w / d.cellArea);
            out.maximum = std::max(out.maximum, w / d.cellArea);
            out.centroid = out.centroid + Vec3{out.x[x], out.y[y], 0} * w;
            out.marginalX[x] += w / dx;
            out.marginalY[y] += w / dy;
        }
    if (!std::isfinite(out.power)) throw std::invalid_argument("Мощность детектора слишком велика");
    out.mean = out.power / (width * height);
    out.hasPower = out.power > 0;
    if (!out.hasPower) return out;
    out.centroid = out.centroid / out.power;
    std::vector<std::pair<double, double>> radii;
    double variance = 0;
    for (int y = 0; y < d.ny; ++y)
        for (int x = 0; x < d.nx; ++x) {
            const double w = d.watts[y * d.nx + x];
            const double a = out.x[x] - out.centroid.x, b = out.y[y] - out.centroid.y;
            out.rmsX += w * a * a;
            out.rmsY += w * b * b;
            if (w > 0) radii.push_back({std::hypot(a, b), w});
            const double relative = w / d.cellArea / out.mean - 1;
            variance += relative * relative;
        }
    out.rmsX = sqrt(out.rmsX / out.power);
    out.rmsY = sqrt(out.rmsY / out.power);
    out.rmsRadius = std::hypot(out.rmsX, out.rmsY);
    out.coefficientOfVariation = sqrt(variance / d.watts.size());
    std::sort(radii.begin(), radii.end());
    double cumulative = 0;
    bool found50 = false;
    for (auto [r, w] : radii) {
        cumulative += w;
        if (!found50 && cumulative + 1e-12 * out.power >= .5 * out.power) {
            out.radius50 = r;
            found50 = true;
        }
        if (cumulative + 1e-12 * out.power >= .8 * out.power) {
            out.radius80 = r;
            break;
        }
    }
    return out;
}
} // namespace optics
