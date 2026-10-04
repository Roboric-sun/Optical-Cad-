#include "optics/model.hpp"
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace optics;
size_t fieldTypeChecks() {
    size_t checks = 0;
    auto check = [&](bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); };
    auto near = [&](double a, double b, double tolerance, const char* message) {
        check(std::isfinite(a) && std::abs(a-b) <= tolerance, message);
    };
    auto rejects = [&](auto action, const char* message) {
        bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
        check(rejected, message);
    };
    Catalog cat;
    Material glass; glass.name = "CONSTANT_1.5"; glass.nd = 1.5; glass.vd = 0; cat.add(glass);
    auto s = SequentialSystem::demo(); s.surfaces[0].material = glass.name;
    s.fields = {{0,0,1}}; s.wavelengths = {{.4861327,1},{.5875618,1},{.6562725,1}};
    // Independent hand ABCD: phi1=phi2=.01/mm, t/n=10/3 mm.
    const double A=29./30, B=10./3, C=-59./3000, D=29./30;
    s.objectDistance = 200; s.fieldType = FieldType::ObjectHeight;
    Field object{3,-5,1};
    for (double wave : {.4861327,.5875618,.6562725})
        for (auto pupil : {Vec3{},Vec3{.5,-.5,0},Vec3{0,1,0}}) {
            auto ray = pupilRay(s,cat,object,wave,pupil.x,pupil.y);
            near(ray.origin.x,3,1e-12,"Object-height X is a fixed source point");
            near(ray.origin.y,-5,1e-12,"Object-height Y is a fixed source point");
            near(ray.origin.z,-200,1e-12,"Finite source plane uses object distance");
            auto hit = trace(s,cat,ray,false,s.stop);
            check(hit.status == TraceStatus::Complete,"Finite-height bundle reaches STOP");
            near(hit.exitPoint.x,pupil.x*5,1e-8,"Object-height STOP X uses entrance pupil");
            near(hit.exitPoint.y,pupil.y*5,1e-8,"Object-height STOP Y uses entrance pupil");
        }
    object.vuy=.2; object.vly=.4;
    s.stop=1;
    for (double py : {-1.,0.,1.}) {
        auto hit=trace(s,cat,pupilRay(s,cat,object,.5875618,0,py),false,1);
        check(hit.status==TraceStatus::Complete,"Finite-height internal STOP ray completes");
        const double expected=py*(py<0?.6:.8)*5*(A+B/200);
        near(hit.exitPoint.y,expected,1e-7,"Independent finite-object STOP footprint with vignetting");
    }
    s.stop=0; s.fieldType=FieldType::ParaxialImageHeight;
    Field image{.0001,-.0002,1};
    const double reduction=C*200+D;
    s.surfaces.back().thickness=-(A*200+B)/reduction;
    auto ray=pupilRay(s,cat,image,.5875618,0,0);
    near(ray.origin.x,image.x*reduction,1e-13,"Finite image height uses signed Gaussian reduction X");
    near(ray.origin.y,image.y*reduction,1e-13,"Finite image height uses signed Gaussian reduction Y");
    auto hit=trace(s,cat,ray);
    check(hit.status==TraceStatus::Complete,"Gaussian image-height chief ray completes");
    near(hit.image.x,image.x,1e-11,"Real chief ray tends to independent Gaussian X height");
    near(hit.image.y,image.y,1e-11,"Real chief ray tends to independent Gaussian Y height");
    s.defocus=7;
    auto defocused=pupilRay(s,cat,image,.6562725,0,0);
    near((defocused.origin-ray.origin).norm(),0,1e-13,"Defocus does not redefine object field");
    s.objectDistance=0; s.defocus=0; s.surfaces.back().thickness=-A/C;
    ray=pupilRay(s,cat,image,.5875618,0,0);
    near(ray.direction.x/ray.direction.z,-C*image.x,1e-13,"Infinite image height sets slope -C h on primary wave");
    hit=trace(s,cat,ray);
    near(hit.image.y,image.y,1e-11,"Infinite Gaussian image height has correct sign and units");
    // Spectral fields refer to one object, rather than changing angle with glass dispersion.
    auto dispersive=SequentialSystem::demo(); dispersive.fieldType=FieldType::ParaxialImageHeight;
    Field f{1,2,1};
    auto blue=pupilRay(dispersive,cat,f,.4861327,0,0), red=pupilRay(dispersive,cat,f,.6562725,0,0);
    near((blue.direction-red.direction).norm(),0,1e-13,"Image-height angular field is independent of traced wavelength");
    dispersive.primary=0;
    auto primaryBlue=pupilRay(dispersive,cat,f,.4861327,0,0);
    check((primaryBlue.direction-blue.direction).norm()>1e-5,"Changing reference wavelength changes Gaussian field definition");
    Material water; water.name="CONSTANT_1.33"; water.nd=1.33; cat.add(water);
    s.surfaces.back().material=water.name;
    const double immersedC=-.01-.0034+(10./3)*.01*.0034;
    ray=pupilRay(s,cat,f,.5875618,0,0);
    near(ray.direction.y/ray.direction.z,-immersedC*2,1e-13,"Immersed image field uses reduced-angle power, not h/EFL");
    // All established analyses and merit operands consume the same converted field.
    dispersive.primary=1; dispersive.fields={f}; autofocus(dispersive,cat);
    auto equivalent=dispersive; equivalent.fieldType=FieldType::Angle;
    equivalent.fields={angularField(dispersive,cat,f)};
    auto a=spot(dispersive,cat,f,9), b=spot(equivalent,cat,equivalent.fields[0],9);
    near(a.rms,b.rms,1e-12,"Image-height spot agrees with equivalent angular bundle");
    check(a.samples.size()==b.samples.size(),"Image-height spot keeps ray survival");
    auto wa=wavefront(dispersive,cat,f,9), wb=wavefront(equivalent,cat,equivalent.fields[0],9);
    near(wa.rms,wb.rms,1e-12,"OPD reference uses same image-height source");
    auto fa=rayFan(dispersive,cat,f,9), fb=rayFan(equivalent,cat,equivalent.fields[0],9);
    near((fa.tangentialAxis-fb.tangentialAxis).norm(),0,1e-12,"Ray-fan axes use field angles, not height in degrees");
    near(fa.referenceImage.y,fb.referenceImage.y,1e-12,"Ray-fan chief reference is consistent");
    auto da=diffraction(dispersive,cat,f,32), db=diffraction(equivalent,cat,equivalent.fields[0],32);
    near(da.pixelUm,db.pixelUm,1e-12,"Image-height FFT keeps physical pixel scale");
    near(da.mtfY[3],db.mtfY[3],1e-12,"Image-height FFT matches equivalent angular wavefront");
    auto curvature=fieldCurvature(dispersive,cat); near(curvature.x[0],std::sqrt(5.),1e-12,"Field-curvature abscissa uses mm for height fields");
    auto plan=defaultOptimization(dispersive,cat); plan.refocus=false;
    near(evaluateMerit(dispersive,cat,plan).score,evaluateMerit(equivalent,cat,plan).score,1e-12,"Merit function supports height fields");
    auto finite=SequentialSystem::demo(); finite.objectDistance=200; finite.fieldType=FieldType::ObjectHeight;
    finite.fields={{0,-2,1}};
    ParameterSolve solve; solve.parameter=SolveParameter::Thickness; solve.kind=SolveKind::MarginalHeight;
    solve.surface=1; solve.reference=2; solve.field=0; solve.value=0;
    finite.solves={solve}; applySolves(finite,cat);
    hit=trace(finite,cat,pupilRay(finite,cat,finite.fields[0],.5875618,0,1));
    check(hit.status==TraceStatus::Complete,"Object-height ray solve reaches image");
    near(hit.image.y,0,1e-8,"Object-height marginal solve reaches specified height");
    finite.fieldType=FieldType::ParaxialImageHeight;
    const double cached=finite.surfaces.back().thickness;
    rejects([&]{applySolves(finite,cat);},"Circular Gaussian-height/ray-solve dependency rejected");
    near(finite.surfaces.back().thickness,cached,0,"Unsupported solve leaves cached geometry unchanged");
    auto invalid=SequentialSystem::demo(); invalid.fieldType=FieldType::ObjectHeight;
    check(!invalid.validate(cat).empty(),"Object-height infinity is diagnosed");
    rejects([&]{pupilRay(invalid,cat,f,.5875618,0,0);},"Object-height infinity cannot launch rays");
    invalid.objectDistance=10000; invalid.fields={{0,100,1}};
    check(invalid.validate(cat).empty(),"Height above 80 mm is not treated as angle above 80 degrees");
    s.surfaces.back().material="AIR"; s.objectDistance=-D/C;
    rejects([&]{pupilRay(s,cat,f,.5875618,0,0);},"Object at front focal conjugate cannot define finite image height");
    invalid=SequentialSystem::demo(); invalid.fieldType=FieldType(99);
    check(!invalid.validate(cat).empty(),"Unknown field type rejected");
    for(auto type : {FieldType::Angle,FieldType::ObjectHeight,FieldType::ParaxialImageHeight})
        for(auto value : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1e9}) {
            auto bad=SequentialSystem::demo(); bad.fieldType=type; bad.objectDistance=200; bad.fields={{0,value,1}};
            check(!bad.validate(cat).empty(),"Invalid coordinates are rejected for every field type");
        }
    auto afocal=SequentialSystem::demo(); afocal.surfaces.resize(1); afocal.surfaces[0].radius=0;
    afocal.surfaces[0].material="AIR"; afocal.fieldType=FieldType::ParaxialImageHeight;
    check(!afocal.validate(cat).empty(),"Afocal Gaussian height rejected explicitly");
    auto tilted=dispersive; tilted.surfaces[0].tilt.y=2;
    check(!tilted.validate(cat).empty(),"Gaussian height rejects non-centered optics");
    return checks;
}
