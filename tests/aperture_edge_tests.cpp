#include "optics/model.hpp"
#include <stdexcept>
using namespace optics;
size_t apertureEdgeChecks() {
    size_t checks=0;
    auto check=[&](bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);};
    auto near=[&](double a,double b,double eps,const char* message){check(std::isfinite(a)&&std::abs(a-b)<eps,message);};
    Catalog cat; Material glass;glass.name="EDGE_GLASS";glass.nd=1.5;cat.add(glass);
    auto s=SequentialSystem::demo();s.surfaces[0].radius=10;s.surfaces[0].thickness=1;
    s.surfaces[0].semiDiameter=s.surfaces[1].semiDiameter=5;s.surfaces[0].material=glass.name;
    s.surfaces[1].radius=-10;s.surfaces[1].thickness=12;s.fields={{0,0,1}};
    s.wavelengths={{.55,1}};s.primary=0;s.pupilDiameter=4;
    const auto radius=clearSemiDiameters(s);
    const double expected=std::sqrt(10.-.25); // intersection of the two radius-10 spheres
    near(radius[0],expected,1e-10,"Spherical common edge equals independent circle intersection");
    near(radius[1],expected,1e-10,"Both surfaces terminate at the same closed edge");
    check(s.validate(cat).empty(),"Crossing surface continuations do not invalidate central optical volume");
    near(s.surfaces[0].semiDiameter,5,1e-12,"Automatic trim does not overwrite requested diameter");
    near(paraxial(s,cat,.55).efl,1/(.1-.05*.05/1.5),1e-10,"Clipping leaves paraxial power unchanged");
    autofocus(s,cat);
    for(double pupil : {-.9,-.5,0.,.5,.9}) {
        auto path=trace(s,cat,pupilRay(s,cat,{},.55,0,pupil));
        check(path.status==TraceStatus::Complete,"Inner rays still refract through the closed lens");
        near(path.index,1,1e-12,"Trimming preserves material transitions");
    }
    auto sp=spot(s,cat,{},9);check(sp.samples.size()==49,"Complete inner pupil remains available to spot analysis");
    check(std::isfinite(wavefront(s,cat,{},9).rms),"Trimmed lens retains OPD analysis");
    s.pupilDiameter=10;
    for(double pupil : {-1.,-.8,.8,1.}) {
        auto path=trace(s,cat,pupilRay(s,cat,{},.55,0,pupil));
        check(path.status==TraceStatus::Vignetted,"Rays outside physical edge are blocked instead of traversing negative volume");
    }
    sp=spot(s,cat,{},17);
    check(!sp.samples.empty() && sp.samples.size()<sp.launched,"Oversized pupil produces partial results rather than a global failure");
    auto polynomial=s;polynomial.surfaces[0].radius=polynomial.surfaces[1].radius=0;
    polynomial.surfaces[0].asphere[0]=.01;
    const auto pr=clearSemiDiameters(polynomial);
    near(pr[0],std::sqrt(10.),1e-10,"Quartic-plane intersection has its analytic radial root");
    check(polynomial.validate(cat).empty(),"Polynomial common edge retains a valid central domain");
    polynomial.surfaces[0].asphere[0]=.1;polynomial.surfaces[0].asphere[1]=-.01;
    polynomial.surfaces[0].semiDiameter=polynomial.surfaces[1].semiDiameter=std::sqrt(10.);
    const auto ring=clearSemiDiameters(polynomial);
    check(ring[0]<2.1,"A later reopened annulus cannot extend the connected central aperture");
    near(1-sag(polynomial.surfaces[0],0,ring[0]),0,1e-9,"First polynomial edge closes at zero thickness");
    auto domain=s;domain.surfaces.resize(1);domain.surfaces[0].radius=2;domain.surfaces[0].semiDiameter=5;
    const auto dr=clearSemiDiameters(domain);
    check(dr[0]<2 && dr[0]>1.99 && std::isfinite(sag(domain.surfaces[0],0,dr[0])),"Sphere continuations are bounded by their real sag domain");
    check(domain.validate(cat).empty(),"Oversized display extent is trimmed without suppressing the sphere");
    auto invalid=s;invalid.surfaces[0].thickness=-1;
    check(!invalid.validate(cat).empty(),"Negative axial spacing remains an actual error");
    invalid.surfaces[0].thickness=0;
    check(!invalid.validate(cat).empty(),"No central optical volume is not silently accepted");
    auto tilted=s;tilted.surfaces[1].tilt.y=1;
    near(clearSemiDiameters(tilted)[0],5,1e-12,"Centered-edge rule is not falsely applied to tilted bodies");
    auto nested=s;
    nested.surfaces[0].thickness=.3;
    Surface inner;inner.radius=0;inner.thickness=.4;inner.semiDiameter=1;inner.material=glass.name;
    nested.surfaces.insert(nested.surfaces.begin()+1,inner);
    inner.thickness=.3;nested.surfaces.insert(nested.surfaces.begin()+2,inner);
    const auto shell=clearSemiDiameters(nested);
    near(shell[0],expected,1e-10,"Outer shell closes even when smaller internal interfaces separate its faces");
    near(shell[3],expected,1e-10,"Nested outer exit face shares the same edge");
    return checks;
}
