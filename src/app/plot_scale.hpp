#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace plotScale {
// The grid is locked to integer multiples of a physical unit, including zero.
inline double step(double minimum) {
    if (!(minimum > 0) || !std::isfinite(minimum))
        return 1;
    const double decade = std::pow(10., std::floor(std::log10(minimum)));
    for (double factor : {1., 2., 5.})
        if (factor * decade >= minimum)
            return factor * decade;
    return 10 * decade;
}
inline std::vector<double> ticks(double lo, double hi, double pixels, double spacing) {
    std::vector<double> result;
    if (!(hi > lo) || !(pixels > 0))
        return result;
    const auto h = step((hi - lo) / std::clamp(pixels / spacing, 2., 16.));
    const auto first = std::ceil(lo / h), last = std::floor(hi / h);
    for (int i = 0; i < 18 && first + i <= last; ++i)
        result.push_back((first + i) * h);
    return result;
}
} // namespace plotScale
