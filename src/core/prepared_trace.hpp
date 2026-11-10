#pragma once
#include "optics/model.hpp"

namespace optics {
// Geometry belongs to one resolved prescription; keep it local to the analysis.
struct PreparedTrace {
    const SequentialSystem& system;
    const Catalog& catalog;
    SequentialFrames frames;
    std::vector<double> clear;
    bool local, physicalStop;
    PreparedTrace(const SequentialSystem& s, const Catalog& c, bool apertures = true)
        : system(s), catalog(c), frames(sequentialFrames(s)),
          clear(apertures || hasCoordinates(s) ? clearSemiDiameters(s) : std::vector<double>{}),
          local(hasCoordinates(s)), physicalStop(usesPhysicalStop(s)) {}
    RayTrace trace(Ray, bool image = true, size_t through = SIZE_MAX, bool aperture = true) const;
    Ray aim(Field angular, double wavelength, double px, double py) const;
};
} // namespace optics
