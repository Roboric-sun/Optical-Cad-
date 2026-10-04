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
void applySolves(SequentialSystem& s) {
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
            int(a.kind) < 0 || int(a.kind) > 2 || !std::isfinite(a.scale) ||
            !std::isfinite(a.offset) || !std::isfinite(a.value) || !std::isfinite(a.height))
            throw std::invalid_argument("Некорректная связь параметра");
        auto& rule = rules[node(a.surface, a.parameter)];
        if (rule) throw std::invalid_argument("Один параметр имеет несколько связей");
        rule = &a;
        if (a.kind == SolveKind::Pickup && a.reference >= n)
            throw std::invalid_argument("Поверхность-источник связи не существует");
        if (a.kind != SolveKind::Pickup && a.parameter != SolveParameter::Thickness)
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
SequentialSystem resolvedSystem(const SequentialSystem& s) {
    auto out = s;
    applySolves(out);
    out.solves.clear();
    return out;
}
void reindexSolves(SequentialSystem& s, const std::vector<size_t>& map) {
    auto rules = s.solves;
    auto mapped = [&](size_t i) {
        if (i >= map.size() || map[i] == SIZE_MAX)
            throw std::invalid_argument("Поверхность используется связью параметров: сначала удалите связь");
        return map[i];
    };
    std::erase_if(rules, [&](const auto& a) { return a.surface < map.size() && map[a.surface] == SIZE_MAX; });
    for (auto& a : rules) {
        a.surface = mapped(a.surface);
        if (a.kind == SolveKind::Pickup || a.kind == SolveKind::EdgeThickness)
            a.reference = mapped(a.reference);
        if (a.kind == SolveKind::OverallLength) {
            a.first = mapped(a.first);
            a.last = mapped(a.last);
        }
    }
    auto candidate = s;
    candidate.solves = std::move(rules);
    applySolves(candidate);
    s = std::move(candidate);
}
} // namespace optics
