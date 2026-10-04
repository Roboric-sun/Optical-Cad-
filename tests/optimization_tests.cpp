#include "optics/model.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace optics;
size_t optimizationChecks() {
    size_t checks = 0;
    auto check = [&](bool ok, const char* name) {
        ++checks;
        if (!ok) throw std::runtime_error(name);
    };
    auto close = [&](double v, double expected, double tolerance, const char* name) {
        check(std::isfinite(v) && std::abs(v - expected) <= tolerance, name);
    };
    Catalog catalog;
    auto s = SequentialSystem::demo();
    OptimizationPlan plan;
    plan.variables = {{VariableParameter::Defocus, 0, -10, 10, .5}};
    plan.minimumThroughput = 0;
    const auto parax = paraxial(s, catalog, s.wavelengths[s.primary].um);
    plan.operands = {{MeritKind::EFL, -1, parax.efl + 2, 2, 3},
                     {MeritKind::ImageDistance, -1, s.imageZ() - 3, 1, 1},
                     {MeritKind::BFL, -1, parax.bfl, 1, 2}};
    auto evaluation = evaluateMerit(s, catalog, plan);
    close(evaluation.score, sqrt(2.), 1e-12, "Normalized weighted merit agrees with analytical residuals");
    close(evaluation.values[0], parax.efl, 1e-12, "Merit reports EFL in millimetres");
    close(evaluation.contributions[0], 3, 1e-12, "Individual weighted merit contribution");
    close(evaluation.values[2], parax.bfl, 1e-12, "Merit reports BFL on primary wavelength");
    auto plane = s;
    plane.surfaces = {Surface{}};
    plane.surfaces[0].thickness = 20;
    plane.pupilDiameter = 2;
    plane.fields = {{3, 0, 1}, {0, 6, 3}};
    plane.wavelengths = {{.5, 1}, {.5, 2}, {.6, 3}};
    plan.minimumThroughput = 1;
    plan.operands = {{MeritKind::CentroidY, -1, 0, 1, 1}};
    evaluation = evaluateMerit(plane, catalog, plan);
    const double imageY = 20 * tan(6 * deg);
    close(evaluation.values[0], .75 * imageY, 1e-11, "All-field merit reports weighted centroid mean");
    close(evaluation.score, sqrt(.75) * imageY, 1e-11,
          "All-field merit averages squared residuals rather than squaring their mean");
    plan.operands[0].field = 0;
    close(evaluateMerit(plane, catalog, plan).score, 0, 1e-11, "A merit operand selects an individual field");
    plane.surfaces[0].transmission = .5;
    plan.operands = {{MeritKind::Throughput, -1, 1, 1, 1}};
    close(evaluateMerit(plane, catalog, plan).values[0], .5, 1e-12,
          "Throughput includes coating losses and duplicate wavelength weights");
    // Geometric survival stays 1 despite 50% transmission.
    plane.surfaces[0].semiDiameter = .1;
    bool rejected = false;
    try { evaluateMerit(plane, catalog, plan); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Merit rejects strongly vignetted pupils");
    plane.surfaces[0].semiDiameter = 12.5;
    plane.surfaces[0].transmission = 1;
    plane.fields = {{0, 0, 1}};
    plan.minimumThroughput = 0;
    plan.variables = {{VariableParameter::Thickness, 0, 18, 25, 1}};
    plan.operands = {{MeritKind::ImageDistance, -1, 16, 1, 1}};
    plan.iterations = 20;
    auto bounded = plane;
    size_t callbacks = 0, previous = 0;
    auto result = optimize(bounded, catalog, plan, [&](size_t done, size_t maximum, double score) {
        check(done >= previous && done <= maximum && std::isfinite(score), "Optimizer reports bounded monotonic progress");
        previous = done; ++callbacks; return true;
    });
    close(bounded.imageZ(), 18, 1e-12, "Bounded optimization reaches the best boundary instead of crossing it");
    close(result.before, 4, 1e-12, "Optimizer preserves starting score");
    close(result.after, 2, 1e-12, "Optimizer returns score of the committed system");
    check(callbacks > 1 && result.evaluations > 1, "Optimizer evaluates actual candidate systems");
    check(std::is_sorted(result.history.rbegin(), result.history.rend()), "Accepted merit history never worsens");
    close(plane.imageZ(), 20, 1e-12, "Optimizing a copy does not mutate the original system");
    auto cancelled = plane;
    auto cancellation = optimize(cancelled, catalog, plan,
                                 [](size_t done, size_t, double) { return done < 4; });
    check(cancellation.cancelled, "Optimizer observes cancellation during a pass");
    close(cancelled.imageZ(), 20, 1e-12, "Cancelled optimization commits no partial geometry");
    auto invalid = plan;
    invalid.variables[0].lower = 21;
    rejected = false;
    try { optimize(plane, catalog, invalid); } catch (const std::exception&) { rejected = true; }
    check(rejected && plane.imageZ() == 20, "Out-of-bound starting parameters are rejected without mutation");
    invalid = plan; invalid.operands[0].scale = 0;
    check(!invalid.validate(plane).empty(), "Zero merit scale is rejected");
    invalid = plan; invalid.variables.push_back(invalid.variables[0]);
    check(!invalid.validate(plane).empty(), "Duplicate optimization variables are rejected");
    invalid = plan; invalid.refocus = true;
    check(!invalid.validate(plane).empty(), "Autofocus cannot override a bounded image thickness variable");
    invalid = plan; invalid.operands[0].field = 5;
    check(!invalid.validate(plane).empty(), "Missing merit field is rejected");
    auto focused = s;
    focused.fields = {{0, 0, 1}};
    focused.wavelengths = {{.5875618, 1}};
    focused.primary = 0;
    autofocus(focused, catalog);
    focused.surfaces.back().thickness += 4;
    OptimizationPlan focusPlan;
    focusPlan.variables = {{VariableParameter::Thickness, 1, 35, 60, .5}};
    focusPlan.operands = {{MeritKind::SpotRMS, -1, 0, .01, 1}};
    auto geometry = focused.surfaces[0];
    auto focusResult = optimize(focused, catalog, focusPlan);
    check(focusResult.after < focusResult.before * .2, "Bounded image-distance optimization improves a physically defocused lens");
    check(focused.surfaces[0].radius == geometry.radius && focused.surfaces[0].material == geometry.material,
          "Unselected optical parameters remain fixed");
    OptimizationPlan aspherePlan;
    aspherePlan.variables = {{VariableParameter::A4, 0, -1e-4, 1e-4, 1e-5}};
    aspherePlan.operands = {{MeritKind::SpotRMS, -1, 0, .01, 1}};
    aspherePlan.refocus = true;
    aspherePlan.iterations = 12;
    auto asphereResult = optimize(focused, catalog, aspherePlan);
    check(asphereResult.after < asphereResult.before && focused.surfaces[0].asphere[0] != 0,
          "Aspheric coefficient is a working optimization variable");
    check(focused.validate(catalog).empty(), "Optimized asphere remains physically valid");
    auto missingField = focused;
    missingField.fields.clear();
    rejected = false;
    try { optimize(missingField, catalog, aspherePlan); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid fields are rejected before autofocus can access them");
    auto missingPrimary = focused;
    missingPrimary.primary = 99;
    rejected = false;
    try { optimize(missingPrimary, catalog, aspherePlan); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Invalid primary wavelength is rejected before refocusing");
    for (int k = 0; k <= int(VariableParameter::Defocus); ++k) {
        auto v = OptimizationVariable{VariableParameter(k), 0};
        check(std::isfinite(variableValue(focused, v)), "Every supported optimization parameter is readable");
    }
    auto legacy = s;
    auto legacyResult = optimizeRadii(legacy, catalog, 2);
    check(legacyResult.after <= legacyResult.before && legacy.validate(catalog).empty(),
          "Existing radius optimization remains available and improves its original criterion");
    DetectorData d;
    d.nx = d.ny = 2;
    d.cellArea = 2;
    d.watts = {2, 2, 2, 2};
    auto stats = detectorStatistics(d, 4, 2);
    close(stats.power, 8, 1e-12, "Detector statistics integrate watts");
    close(stats.mean, 1, 1e-12, "Mean irradiance uses the entire active area");
    close(stats.minimum, 1, 1e-12, "Uniform detector minimum irradiance");
    close(stats.maximum, 1, 1e-12, "Uniform detector maximum irradiance");
    close(stats.coefficientOfVariation, 0, 1e-12, "Uniform detector coefficient of variation");
    close(stats.centroid.norm(), 0, 1e-12, "Symmetric detector centroid");
    close(stats.rmsX, 1, 1e-12, "Detector X moment uses local pixel centres");
    close(stats.rmsY, .5, 1e-12, "Detector Y moment uses local pixel centres");
    close(stats.rmsRadius, sqrt(1.25), 1e-12, "Detector radial second moment");
    close(stats.radius50, sqrt(1.25), 1e-12, "Detector 50% encircled power radius");
    close(stats.radius80, sqrt(1.25), 1e-12, "Detector 80% encircled power radius");
    close(std::accumulate(stats.marginalX.begin(), stats.marginalX.end(), 0.) * 2, stats.power,
          1e-12, "X marginal integrates to detector power");
    close(std::accumulate(stats.marginalY.begin(), stats.marginalY.end(), 0.), stats.power,
          1e-12, "Y marginal integrates to detector power");
    d.watts = {0, 0, 0, 4};
    stats = detectorStatistics(d, 4, 2);
    close(stats.centroid.x, 1, 1e-12, "Off-axis binned footprint centroid X");
    close(stats.centroid.y, .5, 1e-12, "Off-axis binned footprint centroid Y");
    close(stats.rmsRadius, 0, 1e-12, "Single-pixel footprint has zero binned RMS");
    close(stats.coefficientOfVariation, sqrt(3.), 1e-12, "Dark pixels are included in uniformity metric");
    d.watts = {0, 0, 0, 0};
    stats = detectorStatistics(d, 4, 2);
    check(!stats.hasPower && stats.mean == 0, "Zero-power detector has explicit undefined moments");
    d.watts[0] = -1;
    rejected = false;
    try { detectorStatistics(d, 4, 2); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Negative detector cell power is rejected");
    d.watts[0] = std::numeric_limits<double>::quiet_NaN();
    rejected = false;
    try { detectorStatistics(d, 4, 2); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Nonfinite detector cell power is rejected");
    d.watts = {0, 0, 0, 0};
    rejected = false;
    try { detectorStatistics(d, 5, 2); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Detector moments reject inconsistent dimensions and cell area");
    return checks;
}
