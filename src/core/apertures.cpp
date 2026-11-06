#include "optics/model.hpp"
#include <algorithm>

namespace optics {
std::vector<double> clearSemiDiameters(const SequentialSystem& s) {
    std::vector<double> radius;
    for (const auto& surface : s.surfaces) {
        double r = surface.semiDiameter;
        if (surface.radius != 0 && 1 + surface.conic > 0) {
            const double domain = std::abs(surface.radius) / std::sqrt(1 + surface.conic);
            if (r > domain) r = domain * (1 - 1e-12);
        }
        radius.push_back(r);
    }
    if(hasCoordinates(s)||usesAdvancedSurfaces(s))return radius;
    // Each edge clips both adjoining surfaces, so later edges cannot reopen
    // an outer annulus disconnected from the axial optical volume.
    for (size_t i = 0; i + 1 < s.surfaces.size(); ++i) {
        const auto& a = s.surfaces[i]; const auto& b = s.surfaces[i + 1];
        if (a.kind != SurfaceKind::Refract || b.kind != SurfaceKind::Refract ||
            a.decenter.norm2() != 0 || b.decenter.norm2() != 0 ||
            a.tilt.norm2() != 0 || b.tilt.norm2() != 0 ||
            !std::isfinite(a.thickness) || a.thickness < 0) continue;
        const double outer = std::min(radius[i], radius[i + 1]);
        if (!(outer > 0) || !std::isfinite(outer)) continue;
        const auto gap = [&](double y) { return a.thickness + sag(b, 0, y) - sag(a, 0, y); };
        const auto sphere = [](const Surface& surface) {
            return surface.conic == 0 &&
                std::all_of(surface.asphere.begin(), surface.asphere.end(), [](double v){return v == 0;}) &&
                std::all_of(surface.oddAsphere.begin(), surface.oddAsphere.end(), [](double v){return v == 0;});
        };
        // Spherical/planar gap is monotonic. General polynomials are checked
        // on a radial grid before the first crossing is refined by bisection.
        const int samples = sphere(a) && sphere(b) ? 1 : 128;
        double previous = 0;
        for (int j = 1; j <= samples; ++j) {
            const double y = outer * j / samples;
            const double thickness = gap(y);
            if (std::isfinite(thickness) && thickness < -1e-10) {
                double lo = previous, hi = y;
                for (int k = 0; k < 60; ++k) {
                    const double mid = (lo + hi) / 2;
                    if (gap(mid) >= 0) lo = mid; else hi = mid;
                }
                radius[i] = std::min(radius[i], lo);
                radius[i + 1] = std::min(radius[i + 1], lo);
                break;
            }
            previous = y;
        }
    }
    // A nested element can have smaller internal interfaces (e.g. the lens
    // nucleus). Its outer shell must still close where its own faces meet.
    const auto vertices = s.vertices();
    for (size_t i = 0; i + 2 < s.surfaces.size(); ++i) {
        const auto& a = s.surfaces[i];
        const auto centered=[](const Surface& face) {
            return face.kind!=SurfaceKind::Mirror && face.decenter.norm2()==0 && face.tilt.norm2()==0;
        };
        if(a.kind!=SurfaceKind::Refract || !centered(a) || !centered(s.surfaces[i+1]))continue;
        double inner = radius[i + 1];
        for (size_t j = i + 2; j < s.surfaces.size(); ++j) {
            const auto& b = s.surfaces[j];
            if(!centered(b))break;
            const double outer = std::min(radius[i], radius[j]);
            if (inner < outer && a.kind == SurfaceKind::Refract && b.kind == SurfaceKind::Refract &&
                a.decenter.norm2() == 0 && b.decenter.norm2() == 0 &&
                a.tilt.norm2() == 0 && b.tilt.norm2() == 0) {
                auto gap = [&](double y) {return vertices[j]-vertices[i]+sag(b,0,y)-sag(a,0,y);};
                const bool simple = a.conic == 0 && b.conic == 0 &&
                    std::all_of(a.asphere.begin(),a.asphere.end(),[](double x){return x==0;}) &&
                    std::all_of(b.asphere.begin(),b.asphere.end(),[](double x){return x==0;}) &&
                    std::all_of(a.oddAsphere.begin(),a.oddAsphere.end(),[](double x){return x==0;}) &&
                    std::all_of(b.oddAsphere.begin(),b.oddAsphere.end(),[](double x){return x==0;});
                const int samples = simple ? 1 : 128;
                double previous = 0;
                for(int k=1;k<=samples;++k) {
                    const double y=outer*k/samples;
                    if(gap(y)<-1e-10) {
                        double lo=previous,hi=y;
                        for(int iteration=0;iteration<60;++iteration) {
                            const double mid=(lo+hi)/2;
                            if(gap(mid)>=0)lo=mid;else hi=mid;
                        }
                        radius[i]=std::min(radius[i],lo);radius[j]=std::min(radius[j],lo);break;
                    }
                    previous=y;
                }
            }
            inner=std::min(inner,radius[j]);
        }
    }
    return radius;
}
} // namespace optics
