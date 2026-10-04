#include "optics/model.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

using namespace optics;
size_t marginalChecks() {
    size_t checks = 0;
    auto check = [&](bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); };
    auto close = [&](double a, double b, const char* message) {
        check(std::isfinite(a) && std::abs(a - b) < 1e-7, message);
    };
    Catalog c;
    Material glass; glass.name = "REFERENCE_N150"; glass.nd = 1.5;
    c.add(glass);
    SequentialSystem s;
    Surface front; front.radius = 50; front.material = glass.name; front.thickness = 7;
    s.surfaces = {front}; s.fields = {{0,0,1}}; s.wavelengths = {{.55,1}}; s.primary = 0;
    ParameterSolve rule;
    rule.parameter = SolveParameter::Thickness; rule.kind = SolveKind::MarginalHeight;
    rule.surface = 0; rule.reference = 1; rule.value = 0; rule.pupil = 1;
    s.solves = {rule};
    // Independent spherical intersection and signed Snell angles, without core geometry helpers.
    const double h = 5, r = 50, n = 1.5;
    const double sag0 = r - std::sqrt(r*r-h*h);
    const double angle = std::asin(h/r) - std::asin(h/(r*n));
    const double focus = sag0 + h/std::tan(angle);
    applySolves(s,c);
    close(s.surfaces[0].thickness, focus, "Single refracting surface agrees with analytic Snell focus");
    close(s.imageZ(c), focus, "Catalog-aware image coordinate resolves a ray constraint");
    check(s.solves.size() == 1 && s.validate(c).empty(), "Ray solve preserves metadata and validates custom glass");
    auto selected = [&](const SequentialSystem& system) {
        const auto& a = system.solves.back();
        const size_t w = a.wavelength == SIZE_MAX ? system.primary : a.wavelength;
        return trace(system,c,pupilRay(system,c,system.fields[a.field],system.wavelengths[w].um,0,a.pupil));
    };
    auto path = selected(s);
    check(path.status == TraceStatus::Complete, "Solved ray reaches its image");
    close(path.image.y,0,"Solved marginal ray crosses the optical axis");
    close(path.opl,30+sag0,"OPL retains its documented last-physical-surface boundary");
    s.defocus = 3;
    applySolves(s,c);
    close(s.surfaces[0].thickness,focus-3,"Image ray solve subtracts image defocus from the controlled gap");
    close(selected(s).image.y,0,"Image height stays fixed after changing defocus");
    s.solves[0].value = -1;
    applySolves(s,c);
    close(s.imageZ(c),sag0+6/std::tan(angle),"Signed negative height is resolved rather than clamped");
    s.solves[0].pupil = -1; s.solves[0].value = 1;
    close(selected(s).image.y,1,"Negative pupil coordinate and positive target height work");
    s.solves[0] = rule; s.defocus = 0;
    s.surfaces[0].thickness = 999; // All analyses must ignore stale cached solved values.
    const auto physical = resolvedSystem(s,c);
    check(physical.solves.empty() && s.surfaces[0].thickness == 999,"Values-only ray solve is nonmutating");
    close(spot(s,c,s.fields[0],5).rms,spot(physical,c,s.fields[0],5).rms,"Spot analysis resolves ray-dependent geometry");
    close(wavefront(s,c,s.fields[0],5).rms,wavefront(physical,c,s.fields[0],5).rms,"Wavefront resolves ray-dependent geometry");
    close(paraxial(s,c,.55).efl,paraxial(physical,c,.55).efl,"Paraxial analysis uses the catalog-aware resolver");
    auto reject = [&](SequentialSystem invalid, const char* message) {
        const auto before = invalid;
        bool failed = false;
        try { applySolves(invalid,c); } catch (const std::exception&) { failed = true; }
        check(failed,message);
        for (size_t i=0; i<invalid.surfaces.size(); ++i)
            check(invalid.surfaces[i].radius == before.surfaces[i].radius &&
                  invalid.surfaces[i].thickness == before.surfaces[i].thickness,"Ray solve error is transactional");
        check(!invalid.validate(c).empty(),"Invalid ray constraint is reported by validation");
        check(trace(invalid,c,{}).status == TraceStatus::Invalid,"Invalid ray constraint cannot trace a stale gap");
    };
    auto invalid = s; invalid.solves[0].field = 4; reject(invalid,"Missing ray field rejected");
    invalid=s; invalid.solves[0].wavelength=4; reject(invalid,"Missing ray wavelength rejected");
    invalid=s; invalid.solves[0].pupil=1.01; reject(invalid,"Pupil beyond normalized aperture rejected");
    invalid=s; invalid.solves[0].pupil=std::numeric_limits<double>::quiet_NaN(); reject(invalid,"Nonfinite pupil rejected");
    invalid=s; invalid.solves[0].parameter=SolveParameter::Radius; reject(invalid,"Ray height cannot control a radius");
    invalid=s; invalid.solves[0].reference=0; reject(invalid,"Ray target must be the next physical endpoint");
    invalid=s; invalid.solves[0].value=20; reject(invalid,"Backward propagation or negative gap rejected");
    invalid=s; invalid.solves[0].pupil=0; reject(invalid,"On-axis chief ray has no unique height solution");
    invalid=s; invalid.surfaces[0].radius=0; reject(invalid,"Parallel beam has no unique height solution");
    invalid=s; invalid.fields[0].x=1; reject(invalid,"Nonmeridional field rejected explicitly");
    invalid=s; invalid.surfaces[0].tilt.y=1; reject(invalid,"Tilted ray solve rejected explicitly");
    invalid=s; invalid.surfaces[0].kind=SurfaceKind::Mirror; reject(invalid,"Folded ray solve rejected explicitly");
    invalid=s; invalid.surfaces[0].semiDiameter=1; reject(invalid,"Blocked marginal ray rejected");
    bool failed=false;
    try { applySolves(s); } catch (const std::exception&) { failed=true; }
    check(failed && s.surfaces[0].thickness==999,"Legacy resolver requires a catalog for ray-dependent constraints");
    const auto before=s;
    failed=false; try { autofocus(s,c); } catch (const std::exception&) { failed=true; }
    check(failed && s.surfaces[0].thickness==before.surfaces[0].thickness,"Autofocus cannot replace an image-height constraint");
    // Target on a curved physical surface: independently include target sag and optical distance.
    Surface next; next.radius=-30; next.thickness=10;
    s.surfaces.push_back(next); s.solves[0].reference=1; s.solves[0].value=2;
    applySolves(s,c);
    const double sagNext = -30+std::sqrt(900-4.);
    close(s.surfaces[0].thickness,sag0+3/std::tan(angle)-sagNext,"Curved target includes its signed sag");
    auto ray=pupilRay(s,c,s.fields[0],.55,0,1);
    path=trace(s,c,ray,false);
    check(path.status==TraceStatus::Complete,"Ray reaches the solved curved physical target");
    close(path.points[2].y,2,"Actual curved intersection has requested height");
    close(path.opl,30+sag0+1.5*3/std::sin(angle),"OPL agrees with the independent analytic two-surface path");
    invalid=s; invalid.solves[0].value=20; reject(invalid,"Target height outside physical clear aperture rejected");
    invalid=s; invalid.stop=1; reject(invalid,"STOP after the controlled gap cannot silently introduce a circular ray aim");
    ParameterSolve cycle; cycle.parameter=SolveParameter::Thickness; cycle.surface=0;
    // A two-gap chain: upstream pickup points to the ray-controlled downstream gap.
    auto chained=SequentialSystem::demo();
    auto rear=rule; rear.surface=1; rear.reference=2;
    cycle.reference=1; cycle.scale=.1;
    chained.solves={rear,cycle};
    reject(chained,"Dependency cycle through upstream ray geometry rejected");
    s=SequentialSystem::demo(); s.solves={rear};
    applySolves(s,c);
    auto rayFocus=selected(s);
    close(rayFocus.image.y,0,"Two refracting surfaces solve an image marginal height");
    auto finiteObject=s; finiteObject.objectDistance=200;
    applySolves(finiteObject,c);
    close(selected(finiteObject).image.y,0,"Finite object ray reaches the requested image height");
    check(finiteObject.surfaces[1].thickness>s.surfaces[1].thickness,"Finite conjugate produces a larger image distance");
    auto internalStop=s; internalStop.stop=1;
    applySolves(internalStop,c);
    close(selected(internalStop).image.y,0,"STOP on the controlled surface is aimed before solving its following gap");
    ParameterSolve curvature; curvature.kind=SolveKind::CurvaturePickup;
    curvature.surface=1; curvature.reference=0; curvature.scale=-1;
    s.solves.insert(s.solves.begin(),curvature);
    s.surfaces[0].radius=70;
    applySolves(s,c);
    close(s.surfaces[1].radius,-70,"Ray solve resolves upstream curvature before tracing");
    close(selected(s).image.y,0,"Ray target follows a changed source curvature");
    s.fields.push_back({0,2,1}); s.solves.back().field=1; s.solves.back().value=2;
    close(selected(s).image.y,2,"Selected off-axis meridional field reaches its target");
    s.solves.back().field=0; s.solves.back().value=0; s.solves.back().wavelength=0;
    const double blue=resolvedSystem(s,c).surfaces[1].thickness;
    s.solves.back().wavelength=2;
    check(resolvedSystem(s,c).surfaces[1].thickness>blue,"Selected red wavelength focuses farther than blue");
    s.solves.back().wavelength=SIZE_MAX; s.primary=0;
    close(resolvedSystem(s,c).surfaces[1].thickness,blue,"Primary-wave sentinel follows a changed primary wavelength");
    // Optimizer must update both curvature and ray constraints in every candidate.
    OptimizationPlan plan;
    plan.variables={{VariableParameter::Radius,0,60,100,3}};
    plan.operands={{MeritKind::EFL,-1,80,1,1}};
    plan.refocus=false; plan.minimumThroughput=0; plan.iterations=3;
    const auto result=optimize(s,c,plan);
    check(result.after<result.before,"Free-radius optimization improves merit with a ray-height condition");
    close(selected(s).image.y,0,"Optimizer preserves the ray-height condition");
    close(s.surfaces[1].radius,-s.surfaces[0].radius,"Optimizer preserves combined curvature condition");
    auto moved=s;
    moved.surfaces.insert(moved.surfaces.begin(),Surface{}); moved.stop++;
    reindexSolves(moved,{1,2,3},c);
    check(moved.solves.back().surface==2 && moved.solves.back().reference==3,"Insertion preserves ray target and image identities");
    close(selected(moved).image.y,0,"Reindexed image solve uses the updated upstream geometry");
    auto split=s;
    split.surfaces.push_back(Surface{});
    failed=false; try { reindexSolves(split,{0,1,3},c); } catch(const std::exception&) { failed=true; }
    check(failed,"Inserting a surface between a solved gap and its target is rejected");
    auto deleted=s; deleted.surfaces.pop_back();
    reindexSolves(deleted,{0,SIZE_MAX,1},c);
    check(deleted.solves.empty(),"Removing the ray-controlled surface removes its conditions");
    return checks;
}
