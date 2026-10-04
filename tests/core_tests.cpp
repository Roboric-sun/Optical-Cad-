#include "optics/model.hpp"
#include <iostream>
#include <numeric>
#include <stdexcept>

using namespace optics;
static size_t count = 0;
static void check(bool condition, const char* name) {
    ++count;
    if (!condition)
        throw std::runtime_error(name);
}
static void close(double actual, double expected, double tolerance, const char* name) {
    ++count;
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw std::runtime_error(std::string(name) + ": " + std::to_string(actual) +
                                 " != " + std::to_string(expected));
}
int main() {
    try {
        Catalog cat;
        close(cat.get("N-BK7").index(.5875618), 1.5168000345, 1e-9, "BK7 d-line SCHOTT value");
        close(cat.get("F2").index(.5875618), 1.620040137, 2e-9, "F2 d-line");
        close(cat.get("N-F2").index(.5875618), 1.62005, 5e-6, "N-F2 supplier d-line");
        check(cat.materials.size() > 100, "Manufacturer glass catalog available");
        check(cat.get("N-BK7").index(.4861) > cat.get("N-BK7").index(.6563), "Normal dispersion");
        Material manual;
        manual.name = "CUSTOM";
        manual.nd = 1.5;
        manual.vd = 60;
        close(manual.index(.5875618), 1.5, 1e-12, "Manual nd");
        close((manual.nd - 1) / (manual.index(.4861327) - manual.index(.6562725)), 60, 1e-9,
              "Manual Abbe");
        Vec3 transmitted;
        check(refract({sin(30 * deg), 0, cos(30 * deg)}, {0, 0, 1}, 1, 1.5, transmitted),
              "Snell refraction");
        close(transmitted.x, 1.0 / 3, 1e-12, "Snell angle");
        check(!refract({sin(50 * deg), 0, cos(50 * deg)}, {0, 0, 1}, 1.5, 1, transmitted),
              "Total internal reflection");
        close(fresnel({0, 0, 1}, {0, 0, 1}, 1, 1.5), .04, 1e-12, "Fresnel normal incidence");
        close(reflect({0, 0, 1}, {0, 0, 1}).z, -1, 1e-12, "Mirror direction");
        Pose pose{{1, 2, 3}, {13, 27, 44}};
        Vec3 q{2, -4, 6};
        close((pose.local(pose.world(q)) - q).norm(), 0, 1e-12, "Rigid transform inverse");
        Surface surf;
        surf.radius = 50;
        surf.semiDiameter = 10;
        close(sag(surf, 3, 4), 50 - sqrt(2475), 1e-12, "Sphere sag");
        auto hit = intersectSurface(surf, {}, {{0, 5, -10}, {0, 0, 1}});
        check(hit.has_value(), "Sphere intersection");
        close(hit->point.z, 50 - sqrt(2475), 1e-10, "Front cap selected");
        surf.conic = -1;
        close(sag(surf, 3, 4), .25, 1e-12, "Parabolic sag");
        surf.asphere[0] = 1e-6;
        hit = intersectSurface(surf, {}, {{0, 5, -10}, {0, 0, 1}});
        check(hit.has_value(), "Asphere intersection");
        close(hit->point.z, .250625, 1e-8, "Asphere sag consistency");
        auto s = SequentialSystem::demo();
        check(s.validate(cat).empty(), "Demo validation");
        double n = cat.get("N-BK7").index(s.wavelengths[s.primary].um), R = 50, t = 5;
        double lensPower = (n - 1) * (2 / R - (n - 1) * t / (n * R * R));
        auto p = paraxial(s, cat, s.wavelengths[s.primary].um);
        close(p.efl, 1 / lensPower, 1e-10, "Thick lens lensmaker EFL");
        close(p.matrix[0] * p.matrix[3] - p.matrix[1] * p.matrix[2], 1, 1e-12, "ABCD determinant");
        s.fields = {{0, 0, 1}};
        s.wavelengths = {{.5875618, 1}};
        s.primary = 0;
        auto before = spot(s, cat, s.fields[0]);
        autofocus(s, cat);
        auto after = spot(s, cat, s.fields[0]);
        check(after.rms < before.rms, "Autofocus improves RMS");
        auto central = trace(s, cat, pupilRay(s, cat, s.fields[0], .5875618, 0, 0));
        check(central.status == TraceStatus::Complete, "Axial ray complete");
        close(central.image.norm2(), s.imageZ() * s.imageZ(), 1e-8, "Axial image");
        s.stop = 1;
        auto aimed = trace(s, cat, pupilRay(s, cat, {0, 5, 1}, .5875618, .2, .3), false, 1);
        check(aimed.status == TraceStatus::Complete, "Stop aiming trace");
        double pupilMagnification = 1 - 5 / n * (n - 1) / 50;
        close(aimed.exitPoint.x, pupilMagnification, 1e-6, "Stop aiming with entrance pupil x");
        close(aimed.exitPoint.y, 1.5 * pupilMagnification, 1e-6,
              "Stop aiming with entrance pupil y");
        s.stop = 0;
        s.surfaces[1].semiDiameter = .1;
        auto blocked = trace(s, cat, pupilRay(s, cat, {0, 0, 1}, .5875618, .8, 0));
        check(blocked.status == TraceStatus::Vignetted, "Aperture clipping");
        s.surfaces[1].semiDiameter = 12.5;
        auto wave = wavefront(s, cat, {0, 0, 1}, 17);
        check(wave.samples.size() > 100 && std::isfinite(wave.rms), "Finite wavefront");
        // Perfect paraxial sphere: very small aperture suppresses aberrations.
        s.pupilDiameter = .5;
        autofocus(s, cat);
        auto diff = diffraction(s, cat, {0, 0, 1});
        close(std::accumulate(diff.psf.begin(), diff.psf.end(), 0.0), 1, 1e-12,
              "PSF energy normalization");
        close(diff.mtfX.front(), 1, 1e-12, "MTF zero frequency");
        check(diff.mtfX.back() < .05, "MTF beyond cutoff");
        double cutoff = 1 / (.5875618e-3 * paraxial(s, cat, .5875618).fNumber);
        int k = 8;
        double nu = diff.frequency[k] / cutoff;
        double airy = 2 / pi * (acos(nu) - nu * sqrt(1 - nu * nu));
        close(diff.mtfX[k], airy, .035, "Diffraction limited circular pupil analytic MTF");
        auto geom = geometricMTF(spot(s, cat, {0, 0, 1}));
        close(geom.y[0][0], 1, 1e-12, "Geometric MTF at zero frequency");
        auto curvature = fieldCurvature(s, cat);
        close(curvature.y[0][0], paraxial(s, cat, .5875618).bfl - s.surfaces.back().thickness, 1e-5,
              "On-axis tangential differential focus");
        close(curvature.y[0][0], curvature.y[1][0], 1e-9, "On-axis sagittal/tangential symmetry");
        auto chromatic = chromaticFocus(s, cat);
        check(chromatic.y[0].front() < chromatic.y[0].back(), "Normal chromatic focal shift");
        // Frozen v0.2 results: extending analyses must preserve existing calculations.
        auto regression = SequentialSystem::demo();
        autofocus(regression, cat);
        close(paraxial(regression, cat, .5875618).bfl, 47.5362238, 1e-7,
              "v0.2 singlet BFL regression");
        close(regression.imageZ(), 52.0066657, 1e-7, "v0.2 autofocus image regression");
        const double rms[] = {26.8702346, 37.5166681, 83.2440474};
        const double opd[] = {.44033403, 1.10131791, 2.8979318};
        for (size_t field = 0; field < 3; ++field) {
            close(spot(regression, cat, regression.fields[field]).rms * 1000, rms[field], 1e-6,
                  "v0.2 polychromatic RMS regression");
            auto wf = wavefront(regression, cat, regression.fields[field]);
            close(wf.rms * 1000 / wf.wavelength, opd[field], 1e-7, "v0.2 wavefront RMS regression");
        }
        auto fan = rayFan(regression, cat, {0, 0, 1}, 31);
        check(fan.wavelengths.size() == 3 && fan.tangential.x.size() == 31 &&
                  fan.sagittal.x == fan.tangential.x,
              "Both ray-fan sections cover every wavelength and pupil sample");
        double symmetryError = 0, oddError = 0;
        for (size_t wave = 0; wave < 3; ++wave)
            for (size_t i = 0; i < 31; ++i) {
                symmetryError = std::max(
                    symmetryError, std::abs(fan.tangential.y[wave][i] - fan.sagittal.y[wave][i]));
                oddError = std::max(
                    oddError, std::abs(fan.tangential.y[wave][i] + fan.tangential.y[wave][30 - i]));
            }
        close(symmetryError, 0, 1e-8, "On-axis tangential/sagittal rotational symmetry");
        close(oddError, 0, 1e-8, "On-axis ray-fan odd symmetry");
        auto fanY = rayFan(regression, cat, {0, 3.5, 1}, 31);
        auto fanX = rayFan(regression, cat, {3.5, 0, 1}, 31);
        double rotationError = 0;
        for (size_t wave = 0; wave < 3; ++wave)
            for (size_t i = 0; i < 31; ++i)
                for (const auto pair : {std::pair{&fanY.tangential, &fanX.tangential},
                                        std::pair{&fanY.sagittal, &fanX.sagittal}})
                    rotationError = std::max(
                        rotationError, std::abs(pair.first->y[wave][i] - pair.second->y[wave][i]));
        close(rotationError, 0, 1e-7, "Ray fans follow X/Y field orientation");
        close(fanY.tangential.y[regression.primary][15], 0, 1e-9,
              "Primary chief is the ray-fan reference");
        check(std::abs(fanY.tangential.y[0][15] - fanY.tangential.y[2][15]) > .1,
              "Common chief reference retains lateral colour");
        regression.surfaces[1].semiDiameter = .1;
        auto clippedFan = rayFan(regression, cat, {0, 0, 1}, 31);
        check(std::isnan(clippedFan.tangential.y[1].front()) &&
                  std::isnan(clippedFan.sagittal.y[1].back()) &&
                  std::isfinite(clippedFan.tangential.y[1][15]),
              "Vignetted fan samples are gaps, not false zero aberrations");
        bool rejectedFan = false;
        try {
            rayFan(regression, cat, {0, 0, 1}, 2);
        } catch (const std::invalid_argument&) {
            rejectedFan = true;
        }
        check(rejectedFan, "Invalid fan sampling rejected");
        auto agfBefore = cat.materials.size();
        cat.importAGF("NM TEST 2 0 1.5 60 0\nCD 1.03961212 .00600069867 .231792344 .0200179144 "
                      "1.01046945 103.560653\n");
        check(cat.materials.size() == agfBefore + 1, "AGF import");
        try {
            cat.importAGF("NM BAD 1 0 1.5 60\nCD 1 2 3 4 5 6\n");
            check(false, "Unsupported AGF rejected");
        } catch (const std::invalid_argument&) {
        }
        check(cat.materials.size() == agfBefore + 1, "AGF transactional");
        Scene scene;
        Source src;
        src.coneAngle = 1;
        scene.sources = {src};
        SceneObject det;
        det.kind = ObjectKind::Detector;
        det.pose.position = {0, 0, 50};
        det.size = {20, 20, .1};
        scene.objects = {det};
        scene.rayCount = 2000;
        auto result = traceScene(scene, cat);
        check(result.detected == 2000, "All forward rays hit detector");
        close(result.detectedPower, 1, 1e-12, "Detector integrated watts");
        close(result.detectors[0].totalPower(), 1, 1e-12, "Detector pixel sum");
        auto twice = traceScene(scene, cat);
        check(result.detectors[0].watts == twice.detectors[0].watts,
              "Deterministic Monte Carlo seed");
        SceneObject blocker;
        blocker.kind = ObjectKind::Box;
        blocker.interaction = Interaction::Absorb;
        blocker.size = {20, 20, 2};
        blocker.pose.position = {0, 0, 20};
        scene.objects.push_back(blocker);
        result = traceScene(scene, cat);
        check(result.absorbed == 2000 && result.detected == 0,
              "Nearest collision ignores object ordering");
        scene.objects.pop_back();
        SceneObject mirror;
        mirror.kind = ObjectKind::Mirror;
        mirror.pose.position = {0, 0, 20};
        mirror.size = {20, 20, 1};
        mirror.reflectivity = .9;
        scene.objects.push_back(mirror);
        scene.objects[0].pose.position.z = -20;
        result = traceScene(scene, cat);
        check(result.detected == 2000, "Mirror sends rays backward to detector");
        close(result.detectedPower, .9, 1e-12, "Mirror reflectivity");
        close(result.absorbedPower, .1, 1e-12, "Mirror power loss");
        check(result.pathKinds.size() == result.paths.size() && !result.pathKinds.empty() &&
                  result.pathKinds[0].size() + 1 == result.paths[0].size() &&
                  result.pathKinds[0][0] == 0 && result.pathKinds[0][1] == 1,
              "Displayed segments distinguish direct and reflected rays");
        scene.objects.back().interaction = Interaction::Diffuse;
        auto scattered = traceScene(scene, cat);
        check(!scattered.pathKinds.empty() && scattered.pathKinds[0][1] == 2,
              "Displayed segments identify diffuse scattering");
        auto demo = Scene::demo();
        result = traceScene(demo, cat);
#if defined(__APPLE__) && defined(_LIBCPP_VERSION)
        check(result.detected == 9191 && result.escaped == 809,
              "v0.2 libc++ nonsequential seed/count regression");
        close(result.detectedPower, .9191, 1e-12, "v0.2 libc++ detector power regression");
#else
        // std::uniform_real_distribution is not specified to match across standard libraries.
        check(result.detected > 8900 && result.detected < 9500,
              "Demo Monte Carlo throughput within its statistical range");
        close(result.detectedPower, .9191, .025, "Demo detector power statistical range");
#endif
        close(result.launchedPower,
              result.detectedPower + result.absorbedPower + result.escapedPower +
                  result.truncatedPower,
              1e-10, "Energy conserved through refractive solid");
        check(result.launched ==
                  result.detected + result.absorbed + result.escaped + result.truncated,
              "Every ray has terminal state");
        auto cancelled = traceScene(demo, cat, [](size_t done, size_t) { return done < 512; });
        check(cancelled.cancelled && cancelled.launched == 512,
              "Cancellation reports partial results");
        Scene internal;
        internal.rayCount = 10000;
        Source internalSource;
        internalSource.coneAngle = 1;
        internal.sources = {internalSource};
        SceneObject globe;
        globe.kind = ObjectKind::Sphere;
        globe.size = {4, 4, 4};
        globe.material = "N-BK7";
        SceneObject forwardDetector;
        forwardDetector.kind = ObjectKind::Detector;
        forwardDetector.pose.position.z = 20;
        forwardDetector.size = {20, 20, .1};
        internal.objects = {globe, forwardDetector};
        auto internalResult = traceScene(internal, cat);
        check(internalResult.detectedPower > .94 && internalResult.detectedPower < .985,
              "Source inside glass uses the initial material index");
        close(internalResult.launchedPower,
              internalResult.detectedPower + internalResult.escapedPower +
                  internalResult.absorbedPower + internalResult.truncatedPower,
              1e-10, "Internal source energy balance");
        auto prismScene = Scene::prismDemo(cat);
        auto spectralResult = traceScene(prismScene, cat);
        std::array<size_t, 3> displayed{};
        for (auto source : spectralResult.pathSources)
            ++displayed.at(source);
        check(displayed == std::array<size_t, 3>{27, 27, 26},
              "Displayed rays share their budget between all spectral sources");
        bool alternating = true;
        for (size_t i = 0; i < 12; ++i)
            alternating &=
                spectralResult.pathSources.at(i) == i % 3 &&
                spectralResult.pathWavelengths.at(i) == prismScene.sources[i % 3].wavelength;
        check(alternating, "Reducing display count preserves a representative spectral sample");
        auto noDisplay = prismScene;
        noDisplay.displayRays = 0;
        auto invisibleResult = traceScene(noDisplay, cat);
        check(invisibleResult.paths.empty() &&
                  invisibleResult.detectors[0].watts == spectralResult.detectors[0].watts,
              "Path selection has no effect on Monte Carlo random draws or detector power");
        prismScene.sources.resize(1);
        prismScene.sources[0].wavelength = .5875618;
        prismScene.sources[0].power = 1;
        prismScene.sources[0].coneAngle = 1e-6;
        check(prismScene.validate(cat).empty(), "Prism demo is a valid closed dielectric scene");
        auto prismResult = traceScene(prismScene, cat);
        auto straightPath = [](const SceneTrace& result) -> std::vector<Vec3> {
            for (size_t i = 0; i < result.paths.size(); ++i)
                if (std::all_of(result.pathKinds[i].begin(), result.pathKinds[i].end(),
                                [](unsigned char kind) { return kind == 0; }))
                    return result.paths[i];
            throw std::runtime_error("No transmitted prism path in display sample");
        };
        auto path = straightPath(prismResult);
        check(path.size() == 4, "Prism ray crosses entrance and exit faces before detector");
        double expectedDeviation = 2 * asin(n * sin(30 * deg)) - 60 * deg;
        Vec3 inDirection = (path[1] - path[0]).unit(), outDirection = (path[3] - path[2]).unit();
        close(acos(std::clamp(inDirection.dot(outDirection), -1., 1.)), expectedDeviation, 1e-7,
              "60-degree prism minimum deviation from analytic Snell law");
        close((path[1] - Vec3{-5, 0, 0}).norm(), 0, 1e-5, "Prism entrance is on the left face");
        close((path[2] - Vec3{5, 0, 0}).norm(), 0, 1e-5, "Prism exit is on the right face");
        check(prismResult.detectedPower > .84 && prismResult.detectedPower < .94,
              "Prism includes Fresnel losses to reflected paths");
        close(prismResult.launchedPower,
              prismResult.detectedPower + prismResult.absorbedPower + prismResult.escapedPower +
                  prismResult.truncatedPower,
              1e-10, "Prism energy balance");
        Pose rotation{{4, -3, 9}, {0, 0, 37}};
        auto rotatedPrism = prismScene;
        for (auto& object : rotatedPrism.objects) {
            object.pose.position = rotation.world(object.pose.position);
            object.pose.tilt.z += 37;
        }
        for (auto& source : rotatedPrism.sources) {
            source.pose.position = rotation.world(source.pose.position);
            source.pose.tilt.z += 37;
        }
        auto rotatedResult = traceScene(rotatedPrism, cat);
        auto rotatedPath = straightPath(rotatedResult);
        double poseError = 0;
        for (size_t i = 0; i < path.size(); ++i)
            poseError = std::max(poseError, (rotation.local(rotatedPath.at(i)) - path[i]).norm());
        close(poseError, 0, 1e-7, "Prism intersections are invariant under rigid movement");
        close(rotatedResult.detectedPower, prismResult.detectedPower, 1e-12,
              "Rigid movement preserves prism transmission");
        auto insidePrism = prismScene;
        auto& insideSource = insidePrism.sources[0];
        insideSource.pose = {{0, 0, 0}, {-90, 0, -20}};
        double exitAngle = asin(n * sin(20 * deg));
        Vec3 insideExit{8 * tan(20 * deg), 8, 0};
        auto& endDetector = insidePrism.objects[1];
        endDetector.pose = {insideExit + Vec3{sin(exitAngle), cos(exitAngle), 0} * 30,
                            {-90, 0, -exitAngle / deg}};
        auto insidePrismResult = traceScene(insidePrism, cat);
        auto internalPath = straightPath(insidePrismResult);
        check(internalPath.size() == 3, "Internal prism source exits through an end face");
        Vec3 internalDirection = (internalPath[2] - internalPath[1]).unit();
        close(acos(std::clamp(internalDirection.y, -1., 1.)), exitAngle, 1e-7,
              "Source inside prism starts in the correct refractive medium");
        auto opaquePrism = prismScene;
        opaquePrism.objects[0].interaction = Interaction::Absorb;
        opaquePrism.rayCount = 100;
        for (Vec3 direction :
             {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 1, 0}, Vec3{0, -1, 0}}) {
            Pose aim;
            aim.tilt.y = acos(direction.z) / deg;
            aim.tilt.z = atan2(direction.y, direction.x) / deg;
            opaquePrism.sources[0].pose = aim;
            opaquePrism.sources[0].pose.position = direction * -50;
            opaquePrism.objects[1].pose = aim;
            opaquePrism.objects[1].pose.position = direction * 50;
            auto opaqueResult = traceScene(opaquePrism, cat);
            check(opaqueResult.absorbed == 100 && opaqueResult.detected == 0,
                  "Every prism face closes the solid and blocks incident rays");
        }
        std::cout << count << " physical checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
