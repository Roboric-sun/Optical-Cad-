#include "optics/model.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace optics;
size_t solveChecks() {
    size_t checks = 0;
    auto check = [&](bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); };
    auto close = [&](double actual, double expected, const char* message) {
        check(std::isfinite(actual) && std::abs(actual - expected) < 1e-9, message);
    };
    Catalog catalog;
    auto system = SequentialSystem::demo();
    ParameterSolve pickup;
    pickup.surface = 1; pickup.reference = 0; pickup.scale = -1;
    system.solves = {pickup};
    system.surfaces[0].radius = 60;
    system.surfaces[1].radius = -1; // stale cache is never used for analysis
    auto physical = resolvedSystem(system);
    close(physical.surfaces[1].radius, -60, "Radius pickup follows its source");
    check(physical.solves.empty() && system.surfaces[1].radius == -1, "Resolved snapshot is separate and values-only");
    close(paraxial(system, catalog, .5875618).efl, paraxial(physical, catalog, .5875618).efl, "Paraxial analysis resolves stale radius values");
    auto sp = spot(system, catalog, system.fields[0], 7);
    auto referenceSpot = spot(physical, catalog, physical.fields[0], 7);
    close(sp.rms, referenceSpot.rms, "Spot analysis resolves constraints before sampling");
    close(wavefront(system, catalog, system.fields[0], 7).rms,
          wavefront(physical, catalog, physical.fields[0], 7).rms, "Wavefront uses the same resolved geometry");
    auto ray = pupilRay(system, catalog, system.fields[0], .5875618, 0, .5);
    close((trace(system, catalog, ray).image - trace(physical, catalog, ray).image).norm(), 0, "Low-level trace resolves stale values");
    applySolves(system);
    close(system.surfaces[1].radius, -60, "Applying constraints refreshes cached values");
    check(system.solves.size() == 1, "Applying constraints keeps their metadata");
    system.surfaces.push_back(Surface{});
    auto chain = pickup; chain.surface = 2; chain.reference = 1; chain.scale = .5; chain.offset = 2;
    system.solves.push_back(chain);
    applySolves(system);
    close(system.surfaces[2].radius, -28, "Chained scale and offset resolve in dependency order");
    std::reverse(system.solves.begin(), system.solves.end());
    system.surfaces[0].radius = 80;
    applySolves(system);
    close(system.surfaces[2].radius, -38, "Rule order does not affect dependencies");
    system.surfaces[0].radius = 0;
    applySolves(system);
    close(system.surfaces[1].radius, 0, "Plane radius uses the existing zero convention");

    auto reject = [&](SequentialSystem invalid, const char* message) {
        std::vector<std::pair<double,double>> values;
        for (const auto& a : invalid.surfaces) values.emplace_back(a.radius, a.thickness);
        bool failed = false;
        try { applySolves(invalid); } catch (const std::exception&) { failed = true; }
        check(failed, message);
        for (size_t i = 0; i < values.size(); ++i)
            check(values[i] == std::pair(invalid.surfaces[i].radius, invalid.surfaces[i].thickness), "Failed solve leaves every scalar unchanged");
        check(!invalid.validate(catalog).empty(), "Validation reports an invalid constraint");
        check(trace(invalid, catalog, {}).status == TraceStatus::Invalid, "Invalid constraints cannot trace stale geometry");
    };
    auto invalid = system;
    auto cycle = pickup; cycle.surface = 0; cycle.reference = 2;
    invalid.solves.push_back(cycle);
    reject(invalid, "Multi-surface cycles rejected");
    invalid = system; invalid.solves[0].reference = invalid.solves[0].surface;
    reject(invalid, "Self-reference rejected");
    invalid = system; invalid.solves.push_back(invalid.solves.front());
    reject(invalid, "Duplicate target rejected");
    invalid = system; invalid.solves[0].reference = 100;
    reject(invalid, "Missing source rejected");
    invalid = system; invalid.solves[0].scale = std::numeric_limits<double>::infinity();
    reject(invalid, "Nonfinite coefficient rejected");

    system = SequentialSystem::demo();
    ParameterSolve edge;
    edge.parameter = SolveParameter::Thickness; edge.kind = SolveKind::EdgeThickness;
    edge.surface = 0; edge.reference = 1; edge.height = 10; edge.value = 1;
    system.solves = {pickup, edge};
    applySolves(system);
    close(system.surfaces[0].thickness, 1 + 2 * (50 - std::sqrt(2400.)), "Edge thickness matches the analytic spherical sag");
    close(system.surfaces[0].thickness + sag(system.surfaces[1], 10, 0) - sag(system.surfaces[0], 10, 0), 1, "Physical edge equals its requested thickness");
    system.surfaces[0].radius = 70;
    system.surfaces[0].conic = -1;
    system.surfaces[0].asphere[0] = 1e-6;
    applySolves(system);
    close(system.surfaces[0].thickness + sag(system.surfaces[1], 10, 0) - sag(system.surfaces[0], 10, 0), 1, "Edge constraint follows radius, conic and asphere changes");
    check(system.validate(catalog).empty(), "Resolved edge geometry is valid");
    invalid = system; invalid.surfaces[1].tilt.y = 1;
    reject(invalid, "Tilted edge solve rejected instead of applying an axial approximation");
    invalid = system; invalid.solves.back().height = 100;
    reject(invalid, "Edge height outside aperture rejected");
    invalid = system; invalid.solves.back().surface = 1;
    reject(invalid, "Edge solve at the image endpoint rejected");

    system = SequentialSystem::demo();
    ParameterSolve length;
    length.parameter = SolveParameter::Thickness; length.kind = SolveKind::OverallLength;
    length.surface = 1; length.first = 0; length.last = 2; length.value = 55;
    system.solves = {length}; system.defocus = 2;
    applySolves(system);
    close(system.surfaces[1].thickness, 48, "Total length subtracts independent thickness and defocus");
    close(system.imageZ(), 55, "Image endpoint length includes defocus");
    system.surfaces[0].thickness = 7; system.defocus = -1;
    applySolves(system);
    close(system.imageZ(), 55, "Total length stays fixed when free geometry changes");
    invalid = system; invalid.solves[0].value = 1;
    reject(invalid, "Negative remaining thickness rejected transactionally");
    invalid = system;
    ParameterSolve thickness = pickup; thickness.parameter = SolveParameter::Thickness;
    thickness.surface = 0; thickness.reference = 1; thickness.scale = 1;
    invalid.solves.push_back(thickness);
    reject(invalid, "Cycle through an overall-length sum rejected");
    bool failed = false;
    try { autofocus(system, catalog); } catch (const std::exception&) { failed = true; }
    check(failed && system.imageZ() == 55, "Autofocus preserves a constrained image distance");

    system = SequentialSystem::demo(); system.solves = {pickup, edge};
    system.surfaces[0].semiDiameter = system.surfaces[1].semiDiameter = 10;
    autofocus(system, catalog);
    check(system.solves.size() == 2 && !imageThicknessLinked(system), "Autofocus preserves compatible radius and edge constraints");
    auto plan = defaultOptimization(system, catalog);
    check(std::none_of(plan.variables.begin(), plan.variables.end(), [](auto v) { return v.parameter == VariableParameter::Radius && v.surface == 1; }), "Default optimization excludes dependent radii");
    plan.variables = {{VariableParameter::Radius, 0, 40, 70, 2}};
    plan.operands = {{MeritKind::EFL, -1, 55, 1, 1}};
    plan.minimumThroughput = 0; plan.iterations = 5;
    auto result = optimize(system, catalog, plan);
    check(result.after < result.before, "Free-variable optimization works with dependent geometry");
    close(system.surfaces[1].radius, -system.surfaces[0].radius, "Optimization maintains radius pickup");
    close(system.surfaces[0].thickness + sag(system.surfaces[1], 10, 0) - sag(system.surfaces[0], 10, 0), 1, "Optimization maintains the edge thickness");
    auto legacySystem = system;
    const auto legacy = optimizeRadii(legacySystem, catalog, 1);
    check(std::isfinite(legacy.before) && legacy.after <= legacy.before && legacy.evaluations > 1,
          "Legacy radius optimization still evaluates constrained candidates");
    close(legacySystem.surfaces[1].radius, -legacySystem.surfaces[0].radius, "Legacy optimizer preserves the dependent radius");
    close(legacySystem.surfaces[0].thickness + sag(legacySystem.surfaces[1], 10, 0) - sag(legacySystem.surfaces[0], 10, 0), 1,
          "Legacy optimizer preserves the edge constraint");
    plan.variables[0].surface = 1;
    check(!plan.validate(system).empty(), "Solved variables cannot be optimized independently");

    system = SequentialSystem::demo(); system.solves = {pickup};
    system.surfaces.insert(system.surfaces.begin(), Surface{});
    reindexSolves(system, {1,2,3});
    check(system.solves[0].surface == 2 && system.solves[0].reference == 1, "Insertion preserves physical source and target references");
    auto reordered = system;
    std::swap(reordered.surfaces[1], reordered.surfaces[2]);
    reindexSolves(reordered, {0,2,1,3});
    check(reordered.solves[0].surface == 1 && reordered.solves[0].reference == 2, "Reordering preserves pickup identities");
    auto removed = system;
    removed.surfaces.erase(removed.surfaces.begin() + 1);
    failed = false;
    try { reindexSolves(removed, {0,SIZE_MAX,1,2}); } catch (const std::exception&) { failed = true; }
    check(failed && removed.solves[0].reference == 1, "Deleting a referenced source is rejected before changing metadata");
    system.surfaces.erase(system.surfaces.begin() + 2);
    reindexSolves(system, {0,1,SIZE_MAX,2});
    check(system.solves.empty(), "Deleting a target removes its attached constraint");
    system = SequentialSystem::demo();
    thickness.surface = 1; thickness.reference = 0; thickness.scale = 2; thickness.offset = 3;
    system.solves = {thickness};
    applySolves(system);
    close(system.surfaces[1].thickness, 13, "Thickness pickup applies its scale and offset in millimetres");
    check(imageThicknessLinked(system), "Image thickness pickup is identified as an autofocus conflict");
    system.solves = {pickup, thickness};
    system.surfaces[0].radius = 60;
    system.solves[1].offset = -100;
    reject(system, "Late failure does not commit an earlier valid radius solve");
    system = SequentialSystem::demo();
    system.solves = {pickup, edge};
    system.surfaces[0].semiDiameter = system.surfaces[1].semiDiameter = 10;
    plan.variables[0].surface = 0;
    const auto unchanged = system;
    auto cancelled = optimize(system, catalog, plan, [](size_t, size_t, double) { return false; });
    check(cancelled.cancelled && system.surfaces[0].radius == unchanged.surfaces[0].radius &&
          system.surfaces[0].thickness == unchanged.surfaces[0].thickness &&
          system.solves.size() == unchanged.solves.size(), "Cancelling optimization preserves constrained input and its cached values");
    system.surfaces.resize(500);
    system.solves.clear();
    for (size_t i = 0; i < 499; ++i) {
        auto a = pickup; a.surface = i; a.reference = i + 1; a.scale = 1;
        system.solves.push_back(a);
    }
    system.surfaces.back().radius = 1000;
    applySolves(system);
    close(system.surfaces.front().radius, 1000, "Dependency chain works at the 500-surface limit");

    // Curvature is reciprocal radius; these numeric expectations are independent.
    system = SequentialSystem::demo();
    auto curvature = pickup;
    curvature.kind = SolveKind::CurvaturePickup;
    curvature.scale = -2; curvature.offset = .01;
    system.solves = {curvature};
    applySolves(system);
    close(system.surfaces[1].radius, -100. / 3, "Curvature pickup applies scale and offset to 1/R, not R");
    system.surfaces[0].radius = 100;
    applySolves(system);
    close(system.surfaces[1].radius, -100, "Curvature pickup follows a changed source");
    system.surfaces[0].radius = 0;
    applySolves(system);
    close(system.surfaces[1].radius, 100, "Plane source has zero curvature and permits nonzero offset");
    system.solves[0].offset = 0;
    applySolves(system);
    close(system.surfaces[1].radius, 0, "Zero curvature produces the plane radius sentinel");
    system.surfaces[0].radius = 50;
    system.solves[0].scale = -1; system.solves[0].offset = .02;
    applySolves(system);
    close(system.surfaces[1].radius, 0, "Exact cancellation produces a plane without division by zero");
    system.solves[0].offset = .020000000001;
    applySolves(system);
    check(system.surfaces[1].radius > 1e11 && std::isfinite(system.surfaces[1].radius),
          "Small nonzero curvature is not silently rounded to a plane");
    system.solves = {curvature};
    system.surfaces.push_back(Surface{});
    auto mixed = pickup; mixed.surface = 2; mixed.reference = 1; mixed.scale = .5; mixed.offset = 2;
    system.solves.insert(system.solves.begin(), mixed);
    applySolves(system);
    close(system.surfaces[2].radius, -50. / 3 + 2, "Mixed radius and curvature chain resolves regardless of rule order");
    invalid = system;
    cycle.reference = 2;
    invalid.solves.push_back(cycle);
    reject(invalid, "Cycles across radius and curvature links are rejected transactionally");
    invalid = system; invalid.solves.back().parameter = SolveParameter::Thickness;
    reject(invalid, "Curvature solve cannot target thickness");
    invalid = system; invalid.solves.back().reference = 500;
    reject(invalid, "Curvature solve rejects a missing reference");
    invalid = system; invalid.surfaces[0].radius = 1e-310;
    reject(invalid, "Nonrepresentable source curvature is rejected");
    invalid = system; invalid.surfaces[0].radius = 0;
    invalid.solves.back().offset = std::numeric_limits<double>::denorm_min();
    reject(invalid, "Nonrepresentable derived radius is rejected");
    invalid = system; invalid.surfaces[0].radius = 1e-308;
    invalid.solves.back().scale = 1e308;
    reject(invalid, "Overflow during curvature scaling is rejected");

    system = SequentialSystem::demo();
    curvature.scale = -1; curvature.offset = -.005;
    system.solves = {edge, curvature};
    system.surfaces[0].semiDiameter = system.surfaces[1].semiDiameter = 10;
    applySolves(system);
    close(system.surfaces[1].radius, -40, "Curvature gives the independent -40 mm rear radius");
    close(system.surfaces[0].thickness, 1 + 50 - std::sqrt(2400.) + 40 - std::sqrt(1500.),
          "Edge solve resolves its curvature dependency before evaluating sag");
    const auto resolved = resolvedSystem(system);
    close(paraxial(system, catalog, .5875618).efl, paraxial(resolved, catalog, .5875618).efl,
          "Paraxial analysis uses the resolved curvature geometry");
    autofocus(system, catalog);
    close(system.surfaces[1].radius, -40, "Autofocus preserves curvature metadata and values");
    OptimizationPlan curvaturePlan;
    curvaturePlan.variables = {{VariableParameter::Radius, 0, 40, 90, 5}};
    curvaturePlan.operands = {{MeritKind::EFL, -1, 60, 1, 1}};
    curvaturePlan.iterations = 2; curvaturePlan.pupilGrid = 5;
    const auto curvatureResult = optimize(system, catalog, curvaturePlan);
    check(curvatureResult.after <= curvatureResult.before && curvatureResult.evaluations > 1,
          "Optimization evaluates curved dependent surfaces");
    close(1 / system.surfaces[1].radius, -1 / system.surfaces[0].radius - .005,
          "Configured optimizer preserves curvature pickup");
    close(system.surfaces[0].thickness + sag(system.surfaces[1], 10, 0) - sag(system.surfaces[0], 10, 0), 1,
          "Configured optimizer preserves edge thickness with curvature links");
    system = SequentialSystem::demo(); system.solves = {curvature};
    system.surfaces.insert(system.surfaces.begin(), Surface{});
    reindexSolves(system, {1,2,3});
    check(system.solves[0].surface == 2 && system.solves[0].reference == 1,
          "Insertion preserves curvature pickup identities");
    close(system.surfaces[2].radius, -40, "Reindexed curvature pickup uses the original source");
    removed = system; removed.surfaces.erase(removed.surfaces.begin() + 1);
    failed = false;
    try { reindexSolves(removed, {0,SIZE_MAX,1,2}); } catch (const std::exception&) { failed = true; }
    check(failed && removed.solves[0].reference == 1, "Removing curvature source cannot redirect its reference");
    return checks;
}
