#include "optics/model.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace optics;
size_t fieldChecks() {
    size_t checks=0;
    auto check=[&](bool ok,const char* m){ ++checks; if(!ok) throw std::runtime_error(m); };
    auto close=[&](double a,double b,double tolerance,const char* m){ check(std::isfinite(a)&&std::abs(a-b)<tolerance,m); };
    Field f{0,0,1,.2,.4,.25,.5};
    const auto p=vignettedPupil(f,.5,-.8);
    close(p.x,.4,1e-12,"Positive X shrink is VUX");
    close(p.y,-.4,1e-12,"Negative Y shrink is VLY");
    const auto q=vignettedPupil(f,-.5,.8);
    close(q.x,-.3,1e-12,"Negative X shrink is VLX");
    close(q.y,.6,1e-12,"Positive Y shrink is VUY");
    check(vignettedPupil(f,0,0).norm2()==0,"Asymmetric vignetting keeps the chief ray at center");
    const auto inverse=nominalPupil(f,p.x,p.y);
    check(inverse.has_value(),"Physical point in compressed pupil is sampled");
    close(inverse->x,.5,1e-12,"Inverse pupil restores nominal X");
    close(inverse->y,-.8,1e-12,"Inverse pupil restores nominal Y");
    check(!nominalPupil(f,.9,0) && !nominalPupil(f,-.7,0),"Physical FFT mask excludes both differently clipped X halves");
    Catalog c;
    SequentialSystem s;
    Surface plane; plane.kind=SurfaceKind::Stop; plane.thickness=100;
    s.surfaces={plane}; s.fields={f}; s.pupilDiameter=10;
    s.wavelengths={{.55,1}}; s.primary=0;
    auto path=trace(s,c,pupilRay(s,c,f,.55,.5,-.8));
    check(path.status==TraceStatus::Complete,"Mapped ray reaches a planar stop and image");
    close(path.points[1].x,2,1e-9,"Physical stop coordinate uses mapped pupil and EPD/2");
    close(path.points[1].y,-2,1e-9,"Signed mapped pupil reaches physical stop");
    f.x=3; f.y=4; s.fields={f}; s.objectDistance=200;
    path=trace(s,c,pupilRay(s,c,f,.55,-.5,.8),false);
    close(path.points[1].x,-1.5,1e-8,"Finite-object field still aims mapped X at stop");
    close(path.points[1].y,3,1e-8,"Finite-object field still aims mapped Y at stop");
    auto reject=[&](Field invalid){
        auto candidate=s; candidate.fields={invalid};
        check(!candidate.validate(c).empty(),"Invalid vignetting is diagnosed by model validation");
        bool failed=false; try { pupilRay(candidate,c,invalid,.55,0,.5); } catch(const std::exception&){ failed=true; }
        check(failed,"Low-level pupil construction rejects invalid factors");
    };
    for(double v : {-0.01,1.,1.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        auto invalid=f; invalid.vux=v; reject(invalid);
        invalid=f; invalid.vlx=v; reject(invalid);
        invalid=f; invalid.vuy=v; reject(invalid);
        invalid=f; invalid.vly=v; reject(invalid);
    }
    s=SequentialSystem::demo(); s.stop=1;
    f={0,4,1,0,0,.3,.2};
    // Independently obtain physical stop radius from the single preceding refractive surface.
    const double index=c.get("N-BK7").index(.5875618);
    const double stopRadius=5*(1-s.surfaces[0].thickness/index*(index-1)/s.surfaces[0].radius);
    for(double y : {-1.,-.5,0.,.5,1.}) {
        auto ray=pupilRay(s,c,f,.5875618,0,y);
        const auto aimed=trace(s,c,ray,false,s.stop,false);
        check(aimed.status==TraceStatus::Complete,"Ray reaches stop behind a powered surface");
        close(aimed.exitPoint.y,y*(y<0?.8:.7)*stopRadius,1e-7,"Vignetting is applied once before physical STOP aiming");
    }
    s=SequentialSystem::demo(); s.fields={{0,0,1}}; s.wavelengths={{.5875618,1}}; s.primary=0;
    s.pupilDiameter=.5; autofocus(s,c);
    Field half{0,0,1,.5,.5,.5,.5};
    const auto full=diffraction(s,c,s.fields[0],64), reduced=diffraction(s,c,half,64);
    close(std::accumulate(reduced.psf.begin(),reduced.psf.end(),0.),1,1e-12,"Vignetted PSF retains unit energy normalization");
    close(reduced.pixelUm,full.pixelUm,1e-12,"Physical PSF pixel scale stays on the original entrance-pupil grid");
    close(reduced.mtfX[0],1,1e-12,"Vignetted MTF remains one at zero frequency");
    const double cutoff=.5/(.5875618e-3*paraxial(s,c,.5875618).fNumber);
    const double rho=reduced.frequency[4]/cutoff;
    const double expected=2/pi*(std::acos(rho)-rho*std::sqrt(1-rho*rho));
    close(reduced.mtfX[4],expected,.045,"Half-diameter physical pupil agrees with analytic circular MTF");
    check(reduced.mtfX[4]<full.mtfX[4]-.1,"Compressed pupil broadens PSF instead of retaining full-pupil MTF");
    close(reduced.mtfX[4],reduced.mtfY[4],1e-9,"Symmetric vignetted pupil keeps X/Y diffraction symmetry");
    auto asymmetric=half; asymmetric.vux=.7;
    const auto ad=diffraction(s,c,asymmetric,64);
    check(std::abs(ad.mtfX[4]-ad.mtfY[4])>.02,"Asymmetric physical pupil changes directional MTF");
    s.pupilDiameter=10; autofocus(s,c);
    s.surfaces[1].semiDiameter=2;
    const auto blocked=spot(s,c,s.fields[0],9), compressed=spot(s,c,half,9);
    check(compressed.samples.size()>blocked.samples.size(),"Compressed field bundle reduces clipping at a downstream surface");
    const auto fan=rayFan(s,c,half,9);
    check(fan.tangential.x.front()==-1 && fan.tangential.x.back()==1,"Ray fan retains nominal pupil labels");
    auto linked=SequentialSystem::demo();
    linked.fields[0].vuy=.4;
    ParameterSolve raySolve; raySolve.kind=SolveKind::MarginalHeight; raySolve.parameter=SolveParameter::Thickness;
    raySolve.surface=1; raySolve.reference=2;
    linked.solves={raySolve}; applySolves(linked,c);
    const auto solved=trace(linked,c,pupilRay(linked,c,linked.fields[0],linked.wavelengths[linked.primary].um,0,1));
    close(solved.image.y,0,1e-8,"Ray-height solve uses the vignetted selected field");
    check(std::isfinite(wavefront(linked,c,linked.fields[0],9).rms),"Wavefront handles a vignetted field with dependent geometry");
    auto plan=defaultOptimization(linked,c);
    plan.variables={{VariableParameter::Radius,0,40,90,3}};
    plan.operands={{MeritKind::EFL,-1,60,1,1}}; plan.minimumThroughput=0; plan.refocus=false; plan.iterations=2;
    const auto optimized=optimize(linked,c,plan);
    check(optimized.after<optimized.before,"Optimization works with a vignetted ray-height constraint");
    close(trace(linked,c,pupilRay(linked,c,linked.fields[0],linked.wavelengths[linked.primary].um,0,1)).image.y,0,1e-8,
          "Optimization preserves the vignetted ray-height target");
    return checks;
}
