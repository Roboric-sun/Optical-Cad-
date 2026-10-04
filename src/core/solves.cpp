#include "optics/model.hpp"
#include <algorithm>
#include <functional>

namespace optics {
bool isSolved(const SequentialSystem& s, SolveParameter parameter, size_t surface) {
    return std::any_of(s.solves.begin(), s.solves.end(), [&](const auto& a) {
        return a.surface == surface && a.parameter == parameter;
    });
}
bool imageThicknessLinked(const SequentialSystem& s) {
    if (s.surfaces.empty()) return false;
    const size_t last = s.surfaces.size() - 1;
    return std::any_of(s.solves.begin(), s.solves.end(), [&](const auto& a) {
        return a.parameter == SolveParameter::Thickness &&
               (a.surface == last || (a.kind == SolveKind::Pickup && a.reference == last) ||
                (a.kind == SolveKind::OverallLength && a.last == s.surfaces.size()));
    });
}
static void applySolvesImpl(SequentialSystem& s, const Catalog* catalog) {
    if (s.solves.empty()) return;
    const size_t n = s.surfaces.size();
    if (n == 0 || n > 500 || s.solves.size() > 2 * n)
        throw std::invalid_argument("Некорректное число поверхностей или связей параметров");
    auto candidate = s;
    std::vector<const ParameterSolve*> rules(2 * n, nullptr);
    auto node = [](size_t surface, SolveParameter parameter) {
        return 2 * surface + size_t(parameter);
    };
    for (const auto& a : s.solves) {
        if (a.surface >= n || int(a.parameter) < 0 || int(a.parameter) > 1 ||
            int(a.kind) < 0 || int(a.kind) > 4 || !std::isfinite(a.scale) ||
            !std::isfinite(a.offset) || !std::isfinite(a.value) || !std::isfinite(a.height))
            throw std::invalid_argument("Некорректная связь параметра");
        auto& rule = rules[node(a.surface, a.parameter)];
        if (rule) throw std::invalid_argument("Один параметр имеет несколько связей");
        rule = &a;
        if ((a.kind == SolveKind::Pickup || a.kind == SolveKind::CurvaturePickup) && a.reference >= n)
            throw std::invalid_argument("Поверхность-источник связи не существует");
        if (a.kind == SolveKind::CurvaturePickup && a.parameter != SolveParameter::Radius)
            throw std::invalid_argument("Связь кривизны управляет только радиусом");
        if ((a.kind == SolveKind::EdgeThickness || a.kind == SolveKind::OverallLength) && a.parameter != SolveParameter::Thickness)
            throw std::invalid_argument("Край и общая длина управляют только толщиной");
        if (a.kind == SolveKind::EdgeThickness) {
            if (a.surface + 1 >= n || a.reference != a.surface + 1 || a.value < 0 || a.height < 0)
                throw std::invalid_argument("Краевая толщина требует двух соседних поверхностей");
            const auto& front = s.surfaces[a.surface];
            const auto& back = s.surfaces[a.surface + 1];
            if (front.tilt.norm2() != 0 || back.tilt.norm2() != 0 ||
                front.decenter.norm2() != 0 || back.decenter.norm2() != 0 ||
                front.kind == SurfaceKind::Mirror || back.kind == SurfaceKind::Mirror)
                throw std::invalid_argument("Краевая толщина требует соосных поверхностей без зеркал");
            if (a.height > std::min(front.semiDiameter, back.semiDiameter))
                throw std::invalid_argument("Высота края выходит за световую апертуру");
        }
        if (a.kind == SolveKind::OverallLength &&
            (a.first >= a.last || a.last > n || a.surface < a.first ||
             a.surface >= a.last || a.value < 0))
            throw std::invalid_argument("Управляемая толщина должна лежать внутри интервала длины");
        if (a.kind == SolveKind::MarginalHeight) {
            if (!catalog) throw std::invalid_argument("Для решения по лучу требуется каталог материалов");
            const size_t wave = a.wavelength == SIZE_MAX ? s.primary : a.wavelength;
            if (a.parameter != SolveParameter::Thickness || a.reference != a.surface + 1 ||
                a.reference > n || a.field >= s.fields.size() || wave >= s.wavelengths.size() ||
                !std::isfinite(a.pupil) || std::abs(a.pupil) > 1)
                throw std::invalid_argument("Некорректная цель, поле, волна или координата зрачка решения по лучу");
            if (s.stop > a.surface)
                throw std::invalid_argument("Решение по лучу требует STOP до управляемого промежутка");
            const auto f = s.fields[a.field];
            const auto w = s.wavelengths[wave];
            if (!finite({f.x, f.y, f.weight}) || f.x != 0 || std::abs(f.y) > 80 || f.weight <= 0 ||
                !std::isfinite(w.um) || w.um < .2 || w.um > 5 ||
                !std::isfinite(s.pupilDiameter) || s.pupilDiameter <= 0 ||
                !std::isfinite(s.objectDistance) || s.objectDistance < 0 || !std::isfinite(s.defocus))
                throw std::invalid_argument("Решение по лучу требует корректного меридионального поля X=0 и апертуры");
            for (size_t j = 0; j <= std::min(a.reference, n - 1); ++j) {
                const auto& surface = s.surfaces[j];
                if (surface.tilt.norm2() != 0 || surface.decenter.norm2() != 0 || surface.kind == SurfaceKind::Mirror)
                    throw std::invalid_argument("Решение по лучу пока поддерживает соосные поверхности без зеркал");
            }
            if (a.reference < n && std::abs(a.value) > s.surfaces[a.reference].semiDiameter)
                throw std::invalid_argument("Заданная высота луча выходит за апертуру следующей поверхности");
        }
    }
    std::vector<unsigned char> state(2 * n, 0);
    std::function<double(size_t)> evaluate = [&](size_t id) -> double {
        auto& surface = candidate.surfaces[id / 2];
        double& result = id % 2 ? surface.thickness : surface.radius;
        if (state[id] == 2) return result;
        if (state[id] == 1) throw std::invalid_argument("Цикл в связях радиусов или толщин");
        state[id] = 1;
        if (const auto* a = rules[id]) {
            switch (a->kind) {
            case SolveKind::Pickup:
                result = a->scale * evaluate(node(a->reference, a->parameter)) + a->offset;
                break;
            case SolveKind::CurvaturePickup: {
                const double radius = evaluate(node(a->reference, SolveParameter::Radius));
                // Radius 0 encodes a plane; its curvature is exactly 0.
                const double source = radius == 0 ? 0 : 1 / radius;
                const double curvature = a->scale * source + a->offset;
                if (!std::isfinite(source) || !std::isfinite(curvature))
                    throw std::invalid_argument("Связь кривизны даёт нечисловое значение");
                result = curvature == 0 ? 0 : 1 / curvature;
                break;
            }
            case SolveKind::EdgeThickness: {
                evaluate(node(a->surface, SolveParameter::Radius));
                evaluate(node(a->surface + 1, SolveParameter::Radius));
                // edge = axial gap + sag(back) - sag(front)
                result = a->value + sag(candidate.surfaces[a->surface], a->height, 0) -
                         sag(candidate.surfaces[a->surface + 1], a->height, 0);
                break;
            }
            case SolveKind::OverallLength:
                result = a->value - (a->last == n ? s.defocus : 0);
                for (size_t j = a->first; j < a->last; ++j)
                    if (j != a->surface) result -= evaluate(node(j, SolveParameter::Thickness));
                break;
            case SolveKind::MarginalHeight: {
                // Resolve only geometry upstream of this gap; dependency cycles still use the DFS.
                double vertex = 0;
                for (size_t j = 0; j <= a->surface; ++j) {
                    evaluate(node(j, SolveParameter::Radius));
                    if (j < a->surface) vertex += evaluate(node(j, SolveParameter::Thickness));
                }
                if (a->reference < n) evaluate(node(a->reference, SolveParameter::Radius));
                auto prefix = candidate;
                prefix.solves.clear();
                prefix.surfaces.resize(a->surface + 1);
                prefix.surfaces.back().thickness = 0;
                const size_t wave = a->wavelength == SIZE_MAX ? s.primary : a->wavelength;
                const auto ray = pupilRay(prefix, *catalog, s.fields[a->field], s.wavelengths[wave].um, 0, a->pupil);
                const auto path = trace(prefix, *catalog, ray, false);
                if (path.status != TraceStatus::Complete || !finite(path.exitPoint) || !finite(path.exitDirection))
                    throw std::invalid_argument("Выбранный луч не проходит поверхности до управляемой толщины");
                if (std::abs(path.exitDirection.z) < 1e-12 || std::abs(path.exitDirection.y) < 1e-12)
                    throw std::invalid_argument("Высота недостижима: выбранный луч параллелен оси или плоскости изображения");
                const double z = path.exitPoint.z + (a->value - path.exitPoint.y) *
                                 path.exitDirection.z / path.exitDirection.y;
                const double targetSag = a->reference < n ? sag(candidate.surfaces[a->reference], 0, a->value) : s.defocus;
                result = z - vertex - targetSag;
                if (!std::isfinite(result) || result < 0 ||
                    (z - path.exitPoint.z) / path.exitDirection.z < 0)
                    throw std::invalid_argument("Заданная высота луча требует отрицательного промежутка или обратного хода");
                if (a->reference < n) {
                    // Check the actual intersection branch, rather than accepting sag alone.
                    const auto hit = intersectSurface(candidate.surfaces[a->reference],
                        Pose{{0, 0, vertex + result}, {}},
                        Ray{path.exitPoint, path.exitDirection, s.wavelengths[wave].um});
                    if (!hit || std::abs(hit->point.y - a->value) > 1e-7)
                        throw std::invalid_argument("Заданная высота недостижима на выбранной ветви поверхности");
                }
                break;
            }
            }
            if (!std::isfinite(result) || (id % 2 && result < 0))
                throw std::invalid_argument("Связь даёт нечисловое значение или отрицательную толщину");
        } else if (!std::isfinite(result))
            throw std::invalid_argument("Источник связи имеет нечисловое значение");
        state[id] = 2;
        return result;
    };
    for (size_t i = 0; i < rules.size(); ++i)
        if (rules[i]) evaluate(i);
    for (size_t i = 0; i < n; ++i) {
        s.surfaces[i].radius = candidate.surfaces[i].radius;
        s.surfaces[i].thickness = candidate.surfaces[i].thickness;
    }
}
void applySolves(SequentialSystem& s) { applySolvesImpl(s, nullptr); }
void applySolves(SequentialSystem& s, const Catalog& c) { applySolvesImpl(s, &c); }
SequentialSystem resolvedSystem(const SequentialSystem& s) {
    auto out = s;
    applySolves(out);
    out.solves.clear();
    return out;
}
SequentialSystem resolvedSystem(const SequentialSystem& s, const Catalog& c) {
    auto out = s;
    applySolves(out, c);
    out.solves.clear();
    return out;
}
static void reindexSolvesImpl(SequentialSystem& s, const std::vector<size_t>& map, const Catalog* c) {
    auto rules = s.solves;
    auto mapped = [&](size_t i) {
        if (i >= map.size() || map[i] == SIZE_MAX)
            throw std::invalid_argument("Поверхность используется связью параметров: сначала удалите связь");
        return map[i];
    };
    std::erase_if(rules, [&](const auto& a) { return a.surface < map.size() && map[a.surface] == SIZE_MAX; });
    for (auto& a : rules) {
        a.surface = mapped(a.surface);
        if (a.kind == SolveKind::Pickup || a.kind == SolveKind::CurvaturePickup || a.kind == SolveKind::EdgeThickness || a.kind == SolveKind::MarginalHeight)
            a.reference = mapped(a.reference);
        if (a.kind == SolveKind::OverallLength) {
            a.first = mapped(a.first);
            a.last = mapped(a.last);
        }
    }
    auto candidate = s;
    candidate.solves = std::move(rules);
    applySolvesImpl(candidate, c);
    s = std::move(candidate);
}
void reindexSolves(SequentialSystem& s, const std::vector<size_t>& map) { reindexSolvesImpl(s, map, nullptr); }
void reindexSolves(SequentialSystem& s, const std::vector<size_t>& map, const Catalog& c) { reindexSolvesImpl(s, map, &c); }
} // namespace optics
