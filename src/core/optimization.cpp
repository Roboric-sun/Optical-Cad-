#include "optics/model.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <set>

namespace optics {
double variableValue(const SequentialSystem& s, const OptimizationVariable& v) {
    if (v.parameter == VariableParameter::Defocus)
        return s.defocus;
    const auto& surface = s.surfaces.at(v.surface);
    switch (v.parameter) {
    case VariableParameter::Radius: return surface.radius;
    case VariableParameter::Thickness: return surface.thickness;
    case VariableParameter::Conic: return surface.conic;
    case VariableParameter::A4: case VariableParameter::A6:
    case VariableParameter::A8: case VariableParameter::A10:
        return surface.asphere[size_t(v.parameter) - size_t(VariableParameter::A4)];
    default: throw std::invalid_argument("Unknown optimization parameter");
    }
}
static void setVariable(SequentialSystem& s, const OptimizationVariable& v, double value) {
    if (v.parameter == VariableParameter::Defocus) {
        s.defocus = value;
        return;
    }
    auto& surface = s.surfaces.at(v.surface);
    switch (v.parameter) {
    case VariableParameter::Radius: surface.radius = value; break;
    case VariableParameter::Thickness: surface.thickness = value; break;
    case VariableParameter::Conic: surface.conic = value; break;
    default: surface.asphere[size_t(v.parameter) - size_t(VariableParameter::A4)] = value;
    }
}
std::vector<std::string> OptimizationPlan::validate(const SequentialSystem& s) const {
    std::vector<std::string> errors;
    if (variables.empty() || variables.size() > 64 || operands.empty() || operands.size() > 64)
        errors.push_back("Нужно 1…64 переменных и 1…64 критериев");
    if (iterations < 1 || iterations > 100 || pupilGrid < 3 || pupilGrid > 33 ||
        !std::isfinite(minimumThroughput) || minimumThroughput < 0 || minimumThroughput > 1)
        errors.push_back("Некорректные параметры оптимизации");
    std::set<std::pair<int, size_t>> seen;
    for (auto& v : variables) {
        const bool defocus = v.parameter == VariableParameter::Defocus;
        if (int(v.parameter) < 0 || int(v.parameter) > int(VariableParameter::Defocus) ||
            (!defocus && v.surface >= s.surfaces.size()) || !std::isfinite(v.lower) ||
            !std::isfinite(v.upper) || !std::isfinite(v.step) || v.lower >= v.upper ||
            !std::isfinite(v.upper - v.lower) || v.step <= 0 || v.step > v.upper - v.lower)
            errors.push_back("Некорректная переменная или её границы");
        if (v.parameter == VariableParameter::Thickness && v.lower < 0)
            errors.push_back("Толщина не может быть отрицательной");
        if ((v.parameter == VariableParameter::Radius && isSolved(s, SolveParameter::Radius, v.surface)) ||
            (v.parameter == VariableParameter::Thickness && isSolved(s, SolveParameter::Thickness, v.surface)))
            errors.push_back("Связанный параметр нельзя назначить независимой переменной оптимизации");
        if (!seen.insert({int(v.parameter), defocus ? 0 : v.surface}).second)
            errors.push_back("Переменная задана дважды");
        if (refocus && (defocus || (v.parameter == VariableParameter::Thickness &&
                                   v.surface + 1 == s.surfaces.size())))
            errors.push_back("Автофокус управляет последней толщиной и дефокусом: уберите их из переменных");
    }
    if (refocus && imageThicknessLinked(s))
        errors.push_back("Автофокус конфликтует со связями толщины до изображения");
    double weights = 0;
    for (auto& o : operands) {
        if (int(o.kind) < 0 || int(o.kind) > int(MeritKind::Throughput) || o.field < -1 ||
            (o.field >= 0 && size_t(o.field) >= s.fields.size()) || !std::isfinite(o.target) ||
            !std::isfinite(o.scale) || o.scale <= 0 || !std::isfinite(o.weight) || o.weight <= 0)
            errors.push_back("Некорректный критерий, поле, масштаб или вес");
        weights += o.weight;
    }
    if (!std::isfinite(weights))
        errors.push_back("Сумма весов слишком велика");
    return errors;
}
OptimizationPlan defaultOptimization(const SequentialSystem& s, const Catalog& c) {
    OptimizationPlan p;
    for (size_t i = 0; i < s.surfaces.size(); ++i) {
        double r = s.surfaces[i].radius;
        if (r == 0 || s.surfaces[i].kind != SurfaceKind::Refract || isSolved(s, SolveParameter::Radius, i))
            continue;
        p.variables.push_back({VariableParameter::Radius, i, std::min(.7 * r, 1.3 * r),
                               std::max(.7 * r, 1.3 * r), std::abs(r) * .05});
        if (p.variables.size() == 63)
            break;
    }
    p.variables.push_back({VariableParameter::Defocus, 0, s.defocus - 10, s.defocus + 10, .5});
    p.operands.push_back({MeritKind::SpotRMS, -1, 0, .01, 1});
    try {
        auto efl = paraxial(s, c, s.wavelengths.at(s.primary).um).efl;
        p.operands.push_back({MeritKind::EFL, -1, efl, std::max(1., std::abs(efl) * .01), 1});
    } catch (const std::exception&) {
        // A geometric RMS objective also works for systems without a paraxial model.
    }
    return p;
}
MeritEvaluation evaluateMerit(const SequentialSystem& s, const Catalog& c,
                             const OptimizationPlan& p) {
    auto errors = p.validate(s);
    if (!errors.empty()) throw std::invalid_argument(errors.front());
    if (!s.solves.empty()) return evaluateMerit(resolvedSystem(s), c, p);
    if (errors.empty())
        errors = s.validate(c);
    if (!errors.empty())
        throw std::invalid_argument(errors.front());
    std::vector<Spot> spots;
    std::vector<double> throughput;
    const bool needSpots = p.minimumThroughput > 0 ||
        std::any_of(p.operands.begin(), p.operands.end(), [](auto o) {
            return o.kind == MeritKind::SpotRMS || o.kind == MeritKind::CentroidX ||
                   o.kind == MeritKind::CentroidY || o.kind == MeritKind::Throughput;
        });
    if (needSpots) {
        double waveWeights = 0;
        std::map<double, std::pair<double, size_t>> groups;
        for (auto w : s.wavelengths) {
            waveWeights += w.weight;
            groups[w.um].first += w.weight;
            ++groups[w.um].second;
        }
        for (auto f : s.fields) {
            auto sp = spot(s, c, f, p.pupilGrid);
            const double incident = double(sp.launched) / s.wavelengths.size() * waveWeights;
            double transmitted = 0, surviving = 0;
            for (auto sample : sp.samples)
                transmitted += sample.power;
            // Count survival using the input spectral weights, independent of coatings/Fresnel.
            for (const auto& [um, group] : groups)
                surviving += group.first / group.second *
                    std::count_if(sp.samples.begin(), sp.samples.end(),
                                  [um](auto a) { return a.wavelength == um; });
            if (incident <= 0 || surviving / incident + 1e-12 < p.minimumThroughput)
                throw std::runtime_error("Слишком много виньетированных лучей для оптимизации");
            throughput.push_back(transmitted / incident);
            spots.push_back(std::move(sp));
        }
    }
    MeritEvaluation out;
    double sum = 0, weights = 0;
    for (auto o : p.operands) {
        auto value = [&](size_t f) {
            switch (o.kind) {
            case MeritKind::SpotRMS:
            case MeritKind::CentroidX:
            case MeritKind::CentroidY: {
                const auto& sp = spots.at(f);
                double power = 0;
                for (auto a : sp.samples) power += a.power;
                if (power <= 0)
                    throw std::runtime_error("Пятно не содержит энергии");
                return o.kind == MeritKind::SpotRMS ? sp.rms :
                       o.kind == MeritKind::CentroidX ? sp.centroid.x : sp.centroid.y;
            }
            case MeritKind::Throughput: return throughput.at(f);
            case MeritKind::EFL: return paraxial(s, c, s.wavelengths[s.primary].um).efl;
            case MeritKind::BFL: return paraxial(s, c, s.wavelengths[s.primary].um).bfl;
            case MeritKind::ImageDistance: return s.imageZ();
            }
            throw std::invalid_argument("Unknown merit operand");
        };
        const bool global = o.kind == MeritKind::EFL || o.kind == MeritKind::BFL ||
                            o.kind == MeritKind::ImageDistance;
        double mean = 0, squares = 0, fieldWeights = 0;
        for (size_t f = 0; f < s.fields.size(); ++f) {
            if (o.field >= 0 && f != size_t(o.field) && !global) continue;
            const double v = value(f), w = global || o.field >= 0 ? 1 : s.fields[f].weight;
            const double residual = (v - o.target) / o.scale;
            mean += w * v;
            squares += w * residual * residual;
            fieldWeights += w;
            if (global || o.field >= 0) break;
        }
        const double contribution = o.weight * squares / fieldWeights;
        if (!std::isfinite(contribution) || !std::isfinite(mean))
            throw std::runtime_error("Функция качества не имеет конечного значения");
        out.values.push_back(mean / fieldWeights);
        out.contributions.push_back(contribution);
        sum += contribution;
        weights += o.weight;
    }
    out.score = sqrt(sum / weights);
    if (!std::isfinite(out.score)) throw std::runtime_error("Некорректная функция качества");
    return out;
}
OptimizationResult optimize(SequentialSystem& s, const Catalog& c, const OptimizationPlan& p,
                            std::function<bool(size_t, size_t, double)> progress) {
    auto errors = p.validate(s);
    if (errors.empty()) errors = s.validate(c);
    if (!errors.empty()) throw std::invalid_argument(errors.front());
    for (auto v : p.variables) {
        double value = variableValue(s, v);
        if (value < v.lower || value > v.upper)
            throw std::invalid_argument("Начальное значение переменной выходит за границы");
    }
    auto best = s;
    applySolves(best);
    if (p.refocus) autofocus(best, c);
    OptimizationResult out;
    out.before = evaluateMerit(best, c, p).score;
    out.after = out.before;
    out.evaluations = 1;
    out.history.push_back(out.after);
    const size_t maximum = 1 + 2 * p.variables.size() * p.iterations;
    auto cancelled = [&] {
        if (progress && !progress(out.evaluations, maximum, out.after)) {
            out.cancelled = true;
            return true;
        }
        return false;
    };
    if (cancelled()) return out;
    double fraction = 1;
    for (size_t pass = 0; pass < p.iterations; ++pass) {
        bool improved = false;
        for (auto v : p.variables) {
            const auto original = best;
            for (double sign : {-1., 1.}) {
                if (cancelled()) return out;
                double next = std::clamp(variableValue(original, v) + sign * v.step * fraction,
                                         v.lower, v.upper);
                if (next == variableValue(original, v)) continue;
                auto trial = original;
                setVariable(trial, v, next);
                ++out.evaluations;
                try {
                    applySolves(trial);
                    if (p.refocus) autofocus(trial, c);
                    double score = evaluateMerit(trial, c, p).score;
                    if (score < out.after) {
                        best = std::move(trial);
                        out.after = score;
                        improved = true;
                    }
                } catch (const std::exception&) {
                    // Invalid geometry and blocked pupils are rejected, never committed.
                }
            }
        }
        out.history.push_back(out.after);
        if (!improved) fraction *= .5;
        if (fraction < 1e-5) break;
    }
    if (cancelled()) return out;
    s = std::move(best);
    return out;
}
} // namespace optics
