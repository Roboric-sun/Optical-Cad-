#include "optics/model.hpp"
#include <limits>
#include <numeric>
#include <stdexcept>
using namespace optics;
size_t releaseChecks() {
    size_t count=0;
    auto check=[&](bool condition,const char* msg){++count; if(!condition) throw std::runtime_error(msg);};
    auto near=[&](double a,double b,double eps,const char* msg){check(std::isfinite(a)&&std::abs(a-b)<eps,msg);};
    auto rejects=[&](auto fn,const char* msg){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,msg);};
    Catalog cat; auto s=SequentialSystem::demo();
    // A single polynomial term: independent sag and normal from its derivative.
    for(int order=3;order<=22;++order) {
        Surface a; a.radius=0; a.semiDiameter=5;
        const double coefficient=1e-3/std::pow(2.,order);
        if(order%2) a.oddAsphere[(order-3)/2]=coefficient; else a.asphere[(order-4)/2]=coefficient;
        near(sag(a,0,2),1e-3,1e-12,"Every aspheric order has its documented radial power");
        auto hit=intersectSurface(a,{},{{0,2,-5},{0,0,1},.55});
        check(bool(hit),"High/odd polynomial intersection converges");
        near(-hit->normal.y/hit->normal.z,order*1e-3/2,1e-10,"Polynomial normal equals analytic derivative");
        near(sag(a,0,0),0,1e-14,"Odd polynomial is regular on axis");
    }
    const double f=paraxial(s,cat,.5875618).efl;
    near(apertureDiameter(s,cat,ApertureType::FNumber,5),f/5,1e-12,"F-number converts using primary focal length");
    near(apertureDiameter(s,cat,ApertureType::ImageNA,.1),2*f*std::tan(std::asin(.1)),1e-11,"Image NA in air uses sine angle");
    s.objectDistance=200;
    near(apertureDiameter(s,cat,ApertureType::ObjectNA,.1),400*std::tan(std::asin(.1)),1e-11,"Finite object NA uses object distance");
    Material water;water.name="WATER_CONST";water.nd=1.33;cat.add(water);s.surfaces.back().material=water.name;
    auto p=paraxial(s,cat,.5875618);
    near(apertureDiameter(s,cat,ApertureType::ImageNA,.2),2*1.33*std::tan(std::asin(.2/1.33))/std::abs(p.matrix[2]+p.matrix[3]/200),1e-10,"Image NA includes exit medium and finite conjugate");
    rejects([&]{apertureDiameter(s,cat,ApertureType::ImageNA,1.33);},"NA cannot reach medium index");
    s=SequentialSystem::demo(); s.fieldType=FieldType::RealImageHeight;
    for(double distance:{0.,200.}) {
        s.objectDistance=distance;
        if(distance>0) s.surfaces.back().thickness=65;
        for(Field field:{Field{1,2,1},Field{-2,-1,1}}) {
            auto result=trace(s,cat,pupilRay(s,cat,field,.5875618,0,0));
            check(result.status==TraceStatus::Complete,"Real-height chief reaches image");
            near(result.image.x,field.x,1e-7,"Nonlinear real X height attained");
            near(result.image.y,field.y,1e-7,"Nonlinear real Y height attained");
            const auto a=angularField(s,cat,field);
            auto angle=s;angle.fieldType=FieldType::Angle;
            auto blue=pupilRay(s,cat,field,.4861327,.3,.4), equivalent=pupilRay(angle,cat,a,.4861327,.3,.4);
            near((blue.origin-equivalent.origin).norm(),0,1e-10,"Spectral real-height field keeps primary-wave object");
        }
    }
    s=SequentialSystem::demo(); const auto original=s;
    auto cancelled=optimizeRadii(s,cat,12,[](size_t,size_t,double){return false;});
    check(cancelled.cancelled,"Legacy radius optimizer responds to cancellation");
    near(s.surfaces.back().thickness,original.surfaces.back().thickness,1e-14,"Cancellation does not commit autofocus");
    near(s.surfaces[0].radius,original.surfaces[0].radius,1e-14,"Cancellation does not commit radius");
    auto invalid=trace(s,cat,{{std::numeric_limits<double>::quiet_NaN(),0,0},{0,0,1},.55});
    check(invalid.status==TraceStatus::Invalid,"Nonfinite launched ray is explicitly invalid");
    s.pupilDiameter=.5; s.fields={{0,0,1}}; s.wavelengths={{.55,1}};s.primary=0;autofocus(s,cat);
    const auto mono=polychromaticDiffraction(s,cat,{},64,33);
    near(std::accumulate(mono.psf.begin(),mono.psf.end(),0.),1,1e-12,"Exit pupil PSF conserves normalized energy");
    near(mono.mtfX[0],1,1e-12,"MTF zero frequency is unity");
    const auto old=diffraction(s,cat,{},64);
    near(mono.pixelUm,old.pixelUm,.02*old.pixelUm,"Exit pupil scale tends to focal scale in paraxial limit");
    const double cutoff=1000/(.55*(f/.5));
    for(int i=2;i<=10;++i) {
        const double nu=mono.frequency[i]/cutoff;
        const double expected=2/pi*(std::acos(nu)-nu*std::sqrt(1-nu*nu));
        near(mono.mtfX[i],expected,.055,"Exit pupil MTF agrees with circular-pupil analytic formula");
    }
    s.wavelengths={{.55,1},{.55,3}};
    const auto same=polychromaticDiffraction(s,cat,{},64,33);
    near(same.pixelUm,mono.pixelUm,1e-12,"Common grid remains unchanged for repeated wavelength");
    double error=0;for(size_t i=0;i<mono.psf.size();++i)error=std::max(error,std::abs(same.psf[i]-mono.psf[i]));
    near(error,0,1e-11,"Splitting a spectral line preserves every PSF pixel");
    s.wavelengths={{.4861327,1},{.6562725,2}};
    const auto poly=polychromaticDiffraction(s,cat,{},64,33);
    near(std::accumulate(poly.psf.begin(),poly.psf.end(),0.),1,1e-12,"Polychromatic PSF energy uses common pixel area");
    s.wavelengths={{.6562725,2},{.4861327,1}};
    const auto reversed=polychromaticDiffraction(s,cat,{},64,33);
    error=0;for(size_t i=0;i<poly.psf.size();++i)error=std::max(error,std::abs(poly.psf[i]-reversed.psf[i]));
    near(error,0,1e-11,"Spectral ordering does not change any on-axis PSF pixel");
    const auto fine=polychromaticDiffraction(s,cat,{},64,49);
    for(size_t i=0;i<12;++i) near(poly.mtfX[i],fine.mtfX[i],.045,"Pupil quadrature converges on useful MTF band");
    s.surfaces[0].tilt.x=1;
    rejects([&]{polychromaticDiffraction(s,cat,{},64,33);},"Exit-pupil model rejects unsupported tilted optics");
    auto expanded=SequentialSystem::demo();expanded.surfaces[0].asphere[9]=1e-30;expanded.surfaces[1].oddAsphere[0]=1e-7;
    near(variableValue(expanded,{VariableParameter::A22,0}),1e-30,1e-40,"Optimizer reads A22 from the correct coefficient");
    near(variableValue(expanded,{VariableParameter::A3,1}),1e-7,1e-15,"Optimizer reads A3 from the odd polynomial");
    auto plan=defaultOptimization(expanded,cat);plan.variables={{VariableParameter::A3,1,-1e-6,1e-6,1e-7}};plan.iterations=1;
    const auto outcome=optimize(expanded,cat,plan);
    check(outcome.evaluations>=2 && outcome.after<=outcome.before,"Odd-asphere optimization evaluates physical candidates within bounds");
    auto bad=SequentialSystem::demo();bad.wavelengths[0].weight=1e308;bad.wavelengths[1].weight=1e308;
    check(!bad.validate(cat).empty(),"Overflowing spectral weight sum is rejected");
    rejects([&]{wavefront(s,cat,{},1);},"Degenerate wavefront grids fail before division by zero");
    rejects([&]{geometricMTF({},100,1);},"Degenerate MTF grids are rejected");
    auto scene=Scene::demo();scene.objects[0].kind=ObjectKind(99);
    rejects([&]{traceScene(scene,cat);},"Public scene API rejects unknown body enums");
    Spot synthetic;synthetic.samples={{0,0,.55,{-1,0,0},1},{0,0,.55,{1,0,0},1},{0,0,.55,{0,0,0},2}};
    auto energy=encircledEnergy(synthetic);
    check(energy.x.size()==2 && energy.x[0]==0 && energy.x[1]==1,"Encircled energy groups equal radii");
    near(energy.y[0][0],.5,1e-14,"Encircled energy is power weighted");
    near(energy.y[0].back(),1,1e-14,"Encircled energy reaches one");
    cat.importAGF("NM CONST_SCHOTT 1 0 1.5 60\nCD 2.25 0 0 0 0 0\nLD .4 .8\n");
    near(cat.get("CONST_SCHOTT").index(.4861),1.5,1e-14,"Schott dispersion evaluates an independent constant-index reference");
    return count;
}
