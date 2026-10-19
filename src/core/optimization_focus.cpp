#include "optics/model.hpp"
#include <algorithm>

namespace optics {
double autofocusAllFields(SequentialSystem& system, const Catalog& catalog, int grid) {
    if(hasCoordinates(system))throw std::invalid_argument("Общий автофокус локальных систем ещё не поддержан");
    if (grid < 3 || grid > 65 || system.fieldType == FieldType::RealImageHeight)
        throw std::invalid_argument("Общий автофокус: сетка 3…65; реальная высота изображения не поддержана");
    if (imageThicknessLinked(system))
        throw std::invalid_argument("Автофокус требует независимой толщины до изображения");
    auto s = resolvedSystem(system, catalog);
    const auto errors = s.validate(catalog);
    if (!errors.empty()) throw std::invalid_argument(errors.front());
    const double base = s.vertices().back();
    double numerator = 0, denominator = 0, fieldWeight = 0;
    for (const auto& field : s.fields) {
        struct Sample { Vec3 a, b; double weight; };
        std::vector<Sample> samples;
        Vec3 ca, cb; double power = 0;
        for (const auto wave : s.wavelengths)
            for (int y = 0; y < grid; ++y) for (int x = 0; x < grid; ++x) {
                const double px = 2.*x/(grid-1)-1, py = 2.*y/(grid-1)-1;
                if (px*px+py*py > 1+1e-12) continue;
                const auto ray = trace(s,catalog,pupilRay(s,catalog,field,wave.um,px,py),false);
                if (ray.status != TraceStatus::Complete || std::abs(ray.exitDirection.z) < 1e-10) continue;
                const Vec3 b = ray.exitDirection/ray.exitDirection.z;
                const Vec3 a = ray.exitPoint + b*(base-ray.exitPoint.z);
                const double w = wave.weight*ray.power;
                samples.push_back({a,b,w}); ca=ca+a*w; cb=cb+b*w; power+=w;
            }
        if (samples.size()<5 || !(power>0)) throw std::runtime_error("Недостаточно лучей для общего автофокуса");
        ca=ca/power; cb=cb/power;
        for (const auto& point : samples) {
            const Vec3 a=point.a-ca, b=point.b-cb;
            const double w=field.weight*point.weight/power;
            numerator+=w*(a.x*b.x+a.y*b.y);
            denominator+=w*(b.x*b.x+b.y*b.y);
        }
        fieldWeight+=field.weight;
    }
    if (!(denominator/fieldWeight>1e-15)) throw std::runtime_error("Коллимированные лучи не имеют конечного фокуса");
    const double distance=-numerator/denominator;
    if (!std::isfinite(distance) || distance<0) throw std::runtime_error("Общий фокус вне допустимой плоскости изображения");
    auto candidate=system;
    candidate.surfaces.back().thickness=distance; candidate.defocus=0;
    applySolves(candidate,catalog);
    const auto invalid=candidate.validate(catalog);
    if(!invalid.empty())throw std::invalid_argument(invalid.front());
    system=std::move(candidate);
    return base+distance;
}
} // namespace optics
