#pragma once
#include "math.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace optics {
struct Material {
    std::string name = "AIR";
    std::array<double, 3> b{}, c{};
    double nd = 1, vd = 0;
    double minWavelength = 0.2, maxWavelength = 5;
    bool schottFormula = false;
    std::array<double, 6> schott{}; // n² = a0 + a1 λ² + a2 λ^-2 + ... + a5 λ^-8
    double indexOffset=0; // wavelength independent manufacturing Δn, applied after dispersion
    double index(double wavelength_um) const;
};
class Catalog {
  public:
    Catalog();
    std::vector<Material> materials;
    const Material& get(const std::string& name) const;
    void add(Material material);
    void importAGF(const std::string& text); // AGF Schott (1), Sellmeier 1 (2)
};
enum class SurfaceKind { Refract, Mirror, Stop };
enum class CoordinateMode { None, Relative, ReturnOrientation, ReturnFrame };
enum class CoordinateOrder { TranslateThenTilt, TiltThenTranslate };
struct CoordinateBreak {
    CoordinateMode mode=CoordinateMode::None;
    CoordinateOrder order=CoordinateOrder::TranslateThenTilt;
    Vec3 shift, tilt;
    int reference=-1; // -1: global frame, otherwise an earlier nominal surface frame
};
enum class SurfaceProfile { Standard, CylinderX, CylinderY, Toroidal, XYPolynomial };
enum class ApertureShape { Circle, Ellipse, Rectangle };
struct SurfaceAperture {
    ApertureShape shape=ApertureShape::Circle;
    double halfX=1, halfY=1, obscuration=0; // outer mask inside semiDiameter, central circular blocker
};
struct Surface {
    std::string name = "Поверхность";
    SurfaceKind kind = SurfaceKind::Refract;
    double radius = 0, thickness = 5, semiDiameter = 12.5, conic = 0;
    std::array<double, 10> asphere{}; // A4, A6, ... A22, mm powers
    std::array<double, 10> oddAsphere{}; // A3, A5, ... A21, radial mm powers
    std::string material = "AIR";
    double transmission = 1, reflectivity = 1;
    Vec3 decenter, tilt;
    CoordinateBreak coordinates; // applied before this surface, inherited downstream
    SurfaceProfile profile=SurfaceProfile::Standard;
    double rotationRadius=0; // toroid X curvature at vertex, zero means flat X
    SurfaceAperture aperture;
    std::array<double,7> xySag{}; // x², xy, y², x³, x²y, xy², y³; flat-base Cartesian sag
};
struct Field {
    double x = 0, y = 0, weight = 1;
    double vux = 0, vlx = 0, vuy = 0, vly = 0; // shrink each signed pupil half, [0,1)
};
enum class FieldType { Angle, ObjectHeight, ParaxialImageHeight, RealImageHeight };
const char* fieldUnit(FieldType);
bool validVignetting(const Field&);
bool hasVignetting(const Field&);
Vec3 vignettedPupil(const Field&, double x, double y);
// Physical normalized pupil -> sampling coordinates; outside the mapped disk returns nullopt.
std::optional<Vec3> nominalPupil(const Field&, double x, double y);
struct Wavelength {
    double um = 0.5875618, weight = 1;
};
enum class SolveParameter { Radius, Thickness };
// Append kinds so saved numeric identifiers keep their meaning.
enum class SolveKind { Pickup, EdgeThickness, OverallLength, CurvaturePickup, MarginalHeight };
struct ParameterSolve {
    SolveParameter parameter = SolveParameter::Radius;
    size_t surface = 0;
    SolveKind kind = SolveKind::Pickup;
    size_t reference = 0;
    double scale = 1, offset = 0; // offset: mm for Pickup, 1/mm for CurvaturePickup
    double value = 0, height = 0; // edge thickness/total length and radial height, mm
    size_t first = 0, last = 0; // sum [first,last); image endpoint (last=N) includes defocus
    size_t field = 0, wavelength = SIZE_MAX; // SIZE_MAX follows the primary wavelength
    double pupil = 1; // signed normalized meridional pupil coordinate, [-1,1]
};
struct SequentialSystem {
    std::string name = "Двояковыпуклая линза";
    std::vector<Surface> surfaces;
    std::vector<Field> fields{{0, 0, 1}, {0, 3.5, 1}, {0, 7, 1}};
    std::vector<Wavelength> wavelengths{{0.4861327, 1}, {0.5875618, 1}, {0.6562725, 1}};
    size_t primary = 1, stop = 0;
    double pupilDiameter = 10, objectDistance = 0, defocus = 0;
    int pupilGrid = 17;
    std::vector<ParameterSolve> solves;
    FieldType fieldType = FieldType::Angle;
    double imageZ() const;
    double imageZ(const Catalog&) const;
    std::vector<double> vertices() const;
    std::vector<double> vertices(const Catalog&) const;
    std::vector<std::string> validate(const Catalog&) const;
    static SequentialSystem demo();
};
bool hasCoordinates(const SequentialSystem&);
bool hasAnamorphicSurfaces(const SequentialSystem&);
bool hasAdvancedApertures(const SequentialSystem&);
bool usesAdvancedSurfaces(const SequentialSystem&);
bool usesPhysicalStop(const SequentialSystem&);
bool insideSurfaceAperture(const Surface&,double x,double y);
struct SequentialFrames {std::vector<RigidFrame> nominal, surfaces; RigidFrame image;};
SequentialFrames sequentialFrames(const SequentialSystem&); // resolved scalar geometry
SequentialFrames sequentialFrames(const SequentialSystem&, const Catalog&);
struct DifferentialFirstOrder {
    std::array<double,16> matrix{}; // (x,y,n*u,n*v) at z=0 -> local image plane
    Vec3 chiefImage, chiefDirection;
    double outputIndex=1;
    bool afocal=false;
};
DifferentialFirstOrder differentialFirstOrder(const SequentialSystem&, const Catalog&, double wavelength, double step=1e-5);
bool validField(const SequentialSystem&, const Field&);
// Convert to the legacy angle convention using the primary-wave Gaussian conjugate.
// Heights remain fixed across wavelengths and when the image is defocused.
Field angularField(const SequentialSystem&, const Catalog&, Field);
bool isSolved(const SequentialSystem&, SolveParameter, size_t surface);
bool imageThicknessLinked(const SequentialSystem&);
// Central connected clear apertures. Centered adjacent refracting surfaces are
// trimmed at their first common edge; requested diameters remain unchanged.
std::vector<double> clearSemiDiameters(const SequentialSystem&);
// Transactional: failure leaves every cached scalar and constraint unchanged.
void applySolves(SequentialSystem&);
// Ray-dependent constraints require the project's actual material catalog.
void applySolves(SequentialSystem&, const Catalog&);
// Values-only snapshot for tracing: resolves once, then drops constraint metadata.
SequentialSystem resolvedSystem(const SequentialSystem&);
SequentialSystem resolvedSystem(const SequentialSystem&, const Catalog&);
// Mapping includes the old image endpoint; SIZE_MAX means a removed surface.
void reindexSolves(SequentialSystem&, const std::vector<size_t>& oldToNew);
void reindexSolves(SequentialSystem&, const std::vector<size_t>& oldToNew, const Catalog&);
struct Ray {
    Vec3 origin, direction{0, 0, 1};
    double wavelength = 0.5875618, power = 1;
};
enum class TraceStatus { Complete, Miss, Vignetted, TotalInternalReflection, Invalid };
struct RayTrace {
    TraceStatus status = TraceStatus::Invalid;
    std::vector<Vec3> points;
    Vec3 exitPoint, exitDirection, image, imageLocal;
    double opl = 0, power = 1, index = 1;
    size_t failedSurface = 0;
};
struct SurfaceHit {
    Vec3 point, normal;
    double distance = 0;
};
double sag(const Surface&, double x, double y);
Vec3 surfaceNormal(const Surface&,double x,double y);
double shapedSag(const Surface&,double x,double y);
Vec3 shapedNormal(const Surface&,double x,double y);
std::optional<SurfaceHit> intersectToroidal(const Surface&,const RigidFrame&,const Ray&,bool);
std::optional<SurfaceHit> intersectXYPolynomial(const Surface&,const RigidFrame&,const Ray&,bool);
std::optional<SurfaceHit> intersectFramedSurface(const Surface&, const RigidFrame&, const Ray&,bool aperture=true);
std::optional<SurfaceHit> intersectSurface(const Surface&, const Pose&, const Ray&,
                                           bool aperture = true);
RayTrace trace(const SequentialSystem&, const Catalog&, Ray, bool image = true,
               size_t through = SIZE_MAX, bool aperture = true);
Ray pupilRay(const SequentialSystem&, const Catalog&, Field, double wavelength, double px,
             double py);
struct Paraxial {
    double efl = 0, bfl = 0, fNumber = 0;
    std::array<double, 4> matrix{};
};
Paraxial paraxial(const SequentialSystem&, const Catalog&, double wavelength);
enum class ApertureType { EntranceDiameter, FNumber, ObjectNA, ImageNA };
// Convert a specification to the EPD stored in the model. NA = n sin(theta);
// ray slopes are propagated with the primary-wave reduced-angle ABCD matrix.
double apertureDiameter(const SequentialSystem&, const Catalog&, ApertureType, double value);
double autofocus(SequentialSystem&, const Catalog&);
// Weighted polychromatic geometric RMS, separate centroid for each field.
double autofocusAllFields(SequentialSystem&, const Catalog&, int pupilGrid = 17);
struct SpotSample {
    double px = 0, py = 0, wavelength = 0;
    Vec3 image;
    double power = 0;
};
struct Spot {
    std::vector<SpotSample> samples;
    Vec3 centroid;
    double rms = 0, maxRadius = 0;
    size_t launched = 0;
};
Spot spot(const SequentialSystem&, const Catalog&, Field, int grid = 0);
struct WaveSample {
    double px = 0, py = 0, opd = 0, power = 0;
};
struct Wavefront {
    std::vector<WaveSample> samples;
    double rms = 0, pv = 0, wavelength = 0;
};
Wavefront wavefront(const SequentialSystem&, const Catalog&, Field, int grid = 25);
struct Diffraction {
    int size = 64;
    double pixelUm = 0;
    std::vector<double> psf, frequency, mtfX, mtfY;
};
Diffraction diffraction(const SequentialSystem&, const Catalog&, Field, int size = 64);
// Scalar exit-pupil quadrature, common physical image grid across all wavelengths.
// Centered refractors, finite exit pupil, modest numerical aperture; throws outside model.
Diffraction polychromaticDiffraction(const SequentialSystem&, const Catalog&, Field,
                                    int size = 64, int pupilGrid = 33);
struct AnalysisCurve {
    std::vector<double> x;
    std::vector<std::vector<double>> y;
};
struct RayFan {
    // Values are image-plane deviations in micrometres from the primary chief ray.
    // A blocked sample is NaN; curves must not bridge those gaps.
    AnalysisCurve tangential, sagittal;
    std::vector<double> wavelengths;
    Vec3 tangentialAxis, sagittalAxis, referenceImage;
};
RayFan rayFan(const SequentialSystem&, const Catalog&, Field, int samples = 61);
AnalysisCurve longitudinalAberration(const SequentialSystem&, const Catalog&, int samples = 31);
AnalysisCurve fieldCurvature(const SequentialSystem&, const Catalog&);
AnalysisCurve chromaticFocus(const SequentialSystem&, const Catalog&, int samples = 41);
AnalysisCurve geometricMTF(const Spot&, double maximumFrequency = 500, int samples = 101);
AnalysisCurve encircledEnergy(const Spot&); // radius in mm about the energy centroid, cumulative fraction
AnalysisCurve distortion(const SequentialSystem&, const Catalog&); // field radius, percent vs Gaussian chief
struct OptimizationStart {
    double before = 0, after = 0;
    size_t evaluations = 0;
    bool valid = false;
};
struct OptimizationResult {
    double before = 0, after = 0;
    size_t evaluations = 0;
    std::vector<double> history;
    bool cancelled = false;
    std::string termination;
    std::vector<OptimizationStart> starts;
};
OptimizationResult optimizeRadii(SequentialSystem&, const Catalog&, size_t iterations = 12,
                                std::function<bool(size_t, size_t, double)> progress = {});

enum class VariableParameter { Radius, Thickness, Conic, A4, A6, A8, A10, Defocus,
    A12, A14, A16, A18, A20, A22, A3, A5, A7, A9, A11, A13, A15, A17, A19, A21 };
int asphericOrder(VariableParameter);
struct OptimizationVariable {
    VariableParameter parameter = VariableParameter::Radius;
    size_t surface = 0; // ignored for Defocus
    double lower = 0, upper = 1, step = .1;
};
enum class MeritKind { SpotRMS, EFL, BFL, ImageDistance, CentroidX, CentroidY, Throughput,
    WavefrontRMS, MTFX, MTFY, Distortion, TotalTrack, EdgeThickness };
struct MeritOperand {
    MeritKind kind = MeritKind::SpotRMS;
    int field = -1; // -1: weighted mean of squared residuals across all fields
    double target = 0, scale = 1, weight = 1;
    double frequency = 50; // MTF: cycles/mm, interpolated on the 64-pixel physical grid
    size_t surface = 0; // EdgeThickness: gap after this surface
    double height = 1; // EdgeThickness: radial coordinate, mm
};
enum class OptimizationAlgorithm { Coordinate, LevenbergMarquardt };
struct OptimizationPlan {
    std::vector<OptimizationVariable> variables;
    std::vector<MeritOperand> operands;
    size_t iterations = 30;
    int pupilGrid = 9;
    double minimumThroughput = .85; // geometric survival, checked for every field
    bool refocus = false;
    OptimizationAlgorithm algorithm = OptimizationAlgorithm::Coordinate; // legacy files retain their method
    size_t starts = 1; // deterministic starts, including the current system
    bool allFieldsFocus = false; // when refocus is enabled
    std::vector<std::string> validate(const SequentialSystem&) const;
};
struct MeritEvaluation {
    double score = 0; // dimensionless weighted RMS of (value-target)/scale
    std::vector<double> values, contributions;
    std::vector<double> residuals; // squared Euclidean norm equals score²
};
double variableValue(const SequentialSystem&, const OptimizationVariable&);
OptimizationPlan defaultOptimization(const SequentialSystem&, const Catalog&);
MeritEvaluation evaluateMerit(const SequentialSystem&, const Catalog&, const OptimizationPlan&);
// Callback returns false to cancel. A cancelled run leaves the system unchanged.
OptimizationResult optimize(SequentialSystem&, const Catalog&, const OptimizationPlan&,
                            std::function<bool(size_t, size_t, double)> progress = {});

// Append new kinds to preserve the numeric identifiers in existing projects.
enum class ObjectKind { Lens, Mirror, Sphere, Box, Cylinder, Detector, Prism };
enum class Interaction { Dielectric, Absorb, Reflect, Diffuse };
struct SceneObject {
    std::string name = "Объект";
    ObjectKind kind = ObjectKind::Sphere;
    Interaction interaction = Interaction::Dielectric;
    Pose pose;
    Vec3 size{20, 20, 20}; // full widths/height/length; sphere radius=size.x/2
    double radius1 = 35, radius2 = -35, reflectivity = 0.95;
    std::string material = "N-BK7";
    int nx = 48, ny = 48;
};
// Isosceles triangle in local X/Z, extruded along Y. Base at -Z, apex at +Z.
// Apex angle = 2 atan(size.x / (2 size.z)); positions are local millimetres.
std::array<Vec3, 6> prismVertices(Vec3 size);
inline constexpr std::array<std::array<int, 4>, 5> prismFaces{
    {{0, 3, 4, 1}, {1, 4, 5, 2}, {2, 5, 3, 0}, {0, 1, 2, 0}, {3, 5, 4, 3}}};
enum class SourceShape { Point, Rectangle, Ellipse };
enum class Distribution { Cone, Cosine, Isotropic };
struct Source {
    std::string name = "Источник";
    Pose pose;
    SourceShape shape = SourceShape::Point;
    Distribution distribution = Distribution::Cone;
    double width = 2, height = 2, coneAngle = 12, power = 1, wavelength = 0.5875618;
};
struct Scene {
    std::string name = "Светодиодный осветитель";
    std::vector<SceneObject> objects;
    std::vector<Source> sources;
    size_t rayCount = 10000, displayRays = 80, maxSegments = 30;
    uint64_t seed = 42;
    std::vector<std::string> validate(const Catalog&) const;
    static Scene demo();
    static Scene prismDemo(const Catalog&);
};
struct DetectorData {
    size_t objectIndex = 0;
    int nx = 0, ny = 0;
    double cellArea = 0;
    std::vector<double> watts;
    size_t hits = 0;
    double totalPower() const;
};
struct DetectorStatistics {
    double power = 0, minimum = 0, maximum = 0, mean = 0, coefficientOfVariation = 0;
    Vec3 centroid; // local detector coordinates, mm; moments use pixel centres
    double rmsX = 0, rmsY = 0, rmsRadius = 0, radius50 = 0, radius80 = 0;
    bool hasPower = false;
    std::vector<double> x, y, marginalX, marginalY; // positions mm, integrated W/mm
};
DetectorStatistics detectorStatistics(const DetectorData&, double width, double height);
struct SceneTrace {
    std::vector<DetectorData> detectors;
    std::vector<std::vector<Vec3>> paths;
    std::vector<size_t> pathSources;
    std::vector<double> pathWavelengths;
    // One kind per displayed segment: 0 direct, 1 after reflection, 2 after scattering.
    std::vector<std::vector<unsigned char>> pathKinds;
    size_t launched = 0, detected = 0, absorbed = 0, escaped = 0, truncated = 0;
    double launchedPower = 0, detectedPower = 0, absorbedPower = 0, escapedPower = 0,
           truncatedPower = 0, seconds = 0;
    bool cancelled = false;
};
SceneTrace traceScene(const Scene&, const Catalog&,
                      std::function<bool(size_t, size_t)> progress = {});
} // namespace optics
