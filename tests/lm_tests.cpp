#include "optics/model.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>
using namespace optics;
size_t lmChecks() {
    size_t count=0;
    auto check=[&](bool ok,const char* message){++count;if(!ok)throw std::runtime_error(message);};
    auto near=[&](double a,double b,double tolerance,const char* message){check(std::isfinite(a)&&std::abs(a-b)<=tolerance,message);};
    auto rejects=[&](auto function,const char* message){bool bad=false;try{function();}catch(const std::exception&){bad=true;}check(bad,message);};
    Catalog c;auto plane=SequentialSystem::demo();
    plane.surfaces={Surface{},Surface{}};plane.surfaces[0].thickness=10;plane.surfaces[1].thickness=20;
    plane.fields={{0,0,1}};plane.wavelengths={{.55,1}};plane.primary=0;plane.pupilDiameter=2;
    OptimizationPlan p;p.algorithm=OptimizationAlgorithm::LevenbergMarquardt;p.minimumThroughput=0;
    p.variables={{VariableParameter::Thickness,0,0,40,1},{VariableParameter::Thickness,1,0,60,2}};
    p.operands={{MeritKind::TotalTrack,-1,17,1,1},{MeritKind::ImageDistance,-1,42,1,3}};
    auto s=plane;size_t previous=0, callbacks=0;
    auto result=optimize(s,c,p,[&](size_t done,size_t maximum,double score){
        check(done>=previous&&done<=maximum&&std::isfinite(score),"DLS progress is monotonic and bounded");previous=done;++callbacks;return true;
    });
    near(s.surfaces[0].thickness,17,1e-7,"DLS solves first coupled linear equation");
    near(s.surfaces[1].thickness,25,1e-7,"DLS solves second coupled linear equation");
    check(result.after<1e-7 && callbacks>5,"DLS evaluates actual optical models and reaches known minimum");
    check(std::is_sorted(result.history.rbegin(),result.history.rend()),"DLS accepted score history is monotonic");
    check(result.starts.size()==1 && result.starts[0].valid && !result.termination.empty(),"DLS reports start comparison and stopping reason");
    auto coordinate=plane;auto old=p;old.algorithm=OptimizationAlgorithm::Coordinate;
    const auto oldResult=optimize(coordinate,c,old);
    check(result.after<oldResult.after && result.evaluations<oldResult.evaluations,"DLS improves the coupled task more efficiently than coordinate search");
    auto bounded=p;bounded.variables[0].upper=15;
    s=plane;result=optimize(s,c,bounded);
    near(s.surfaces[0].thickness,15,1e-8,"DLS respects active upper bound");
    near(s.surfaces[1].thickness,27,1e-6,"DLS continues optimizing other variables at a bound");
    bounded.operands[0].target=-5;s=plane;result=optimize(s,c,bounded);
    near(s.surfaces[0].thickness,0,1e-8,"One-sided differences reach active lower bound");
    near(s.imageZ(),42,1e-6,"Remaining variable solves image distance with bound active");
    auto rankDeficient=p;rankDeficient.variables.push_back({VariableParameter::Conic,0,-1,1,.1});
    s=plane;result=optimize(s,c,rankDeficient);
    check(result.after<1e-6 && s.surfaces[0].conic==0,"Damped QR handles a zero-sensitivity column without arbitrary drift");
    auto scales=p;scales.variables[0].step=1e-3;scales.variables[1].step=5;
    s=plane;result=optimize(s,c,scales);
    check(result.after<1e-6,"Variable scaling preserves the physical optimum");
    auto cancelled=plane;
    const auto stopped=optimize(cancelled,c,p,[](size_t done,size_t,double){return done<4;});
    check(stopped.cancelled && cancelled.surfaces[0].thickness==10 && cancelled.surfaces[1].thickness==20,"Cancelling a DLS Jacobian commits no partial geometry");
    auto multi=p;multi.starts=4;s=plane;result=optimize(s,c,multi);
    near(s.imageZ(),42,1e-6,"Multistart selects a valid improved model");
    check(result.starts.size()==4 && std::is_sorted(result.history.rbegin(),result.history.rend()),"Multistart history preserves global best");
    auto repeat=plane;const auto repeated=optimize(repeat,c,multi);
    check(result.history==repeated.history && result.evaluations==repeated.evaluations,"Multistart is deterministic");
    cancelled=plane;
    const size_t firstRun=result.starts[0].evaluations;
    const auto multiCancel=optimize(cancelled,c,multi,[&](size_t done,size_t,double){return done<firstRun+2;});
    check(multiCancel.cancelled && cancelled.imageZ()==plane.imageZ(),"Cancelling a later start rolls back earlier successful starts too");
    auto linked=plane;linked.solves={{SolveParameter::Thickness,1,SolveKind::Pickup,0,2,1}};
    auto linkedPlan=p;linkedPlan.variables.resize(1);linkedPlan.operands={{MeritKind::ImageDistance,-1,52,1,1}};
    result=optimize(linked,c,linkedPlan);
    near(linked.surfaces[0].thickness,17,1e-6,"DLS differentiates through live pickup solves");
    near(linked.surfaces[1].thickness,35,1e-6,"Dependent thickness is recomputed and preserved");
    auto bad=p;bad.variables[0].upper=9;
    rejects([&]{auto a=plane;optimize(a,c,bad);},"Out-of-bounds DLS start is rejected");
    bad=p;bad.starts=0;check(!bad.validate(plane).empty(),"Zero starts rejected");
    bad=p;bad.algorithm=OptimizationAlgorithm(99);check(!bad.validate(plane).empty(),"Unknown solver rejected");
    auto feasibility=p;feasibility.variables={{VariableParameter::Defocus,0,-100,10,1}};
    feasibility.operands={{MeritKind::ImageDistance,-1,0,1,1},{MeritKind::SpotRMS,-1,0,1,1}};
    feasibility.minimumThroughput=1;
    auto feasible=plane;const auto feasibleResult=optimize(feasible,c,feasibility);
    check(feasibleResult.after<feasibleResult.before && feasible.imageZ()>=feasible.vertices().back(),
          "DLS rejects steps with an image behind the last surface instead of accepting a blocked pupil");
    const auto feasibleSpot=spot(feasible,c,feasible.fields[0],feasibility.pupilGrid);
    check(feasibleSpot.samples.size()==feasibleSpot.launched,"Committed constrained result retains full geometric survival");
    auto restartFeasible=plane;restartFeasible.defocus=-19;feasibility.starts=2;
    const auto invalidStart=optimize(restartFeasible,c,feasibility);
    check(invalidStart.starts.size()==2 && invalidStart.starts[0].valid && !invalidStart.starts[1].valid,
          "Invalid additional start is recorded without discarding the valid baseline result");
    check(restartFeasible.imageZ()>=restartFeasible.vertices().back(),"Invalid restart cannot overwrite a valid optical design");
    auto lens=SequentialSystem::demo();lens.pupilDiameter=3;
    lens.fields={{0,0,1},{0,5,3}};lens.wavelengths={{.4861327,1},{.5875618,2},{.6562725,4}};
    OptimizationPlan metric;metric.minimumThroughput=0;metric.pupilGrid=9;
    metric.variables={{VariableParameter::Defocus,0,-10,10,.5}};
    metric.operands={{MeritKind::SpotRMS,-1,0,1,1}};
    auto measure=[&](const SequentialSystem& a){const auto e=evaluateMerit(a,c,metric);return e.score*e.score;};
    const double z0=lens.imageZ();const double f0=measure(lens);
    auto left=lens,right=lens;left.defocus=-1;right.defocus=1;
    const double fl=measure(left),fr=measure(right);
    const double expected=z0-(fr-fl)/(2*(fr+fl-2*f0));
    auto focus=lens;const double focusedZ=autofocusAllFields(focus,c,9);
    near(focusedZ,expected,1e-7,"All-field spectral focus equals independent three-plane quadratic minimum");
    check(measure(focus)<f0,"All-field focus improves weighted geometric RMS");
    auto reversed=lens;std::reverse(reversed.fields.begin(),reversed.fields.end());std::reverse(reversed.wavelengths.begin(),reversed.wavelengths.end());
    reversed.primary=1;near(autofocusAllFields(reversed,c,9),focusedZ,1e-8,"Common focus is invariant to field and spectral order");
    auto split=lens;split.wavelengths.push_back(split.wavelengths[0]);split.wavelengths[0].weight=.5;split.wavelengths.back().weight=.5;
    near(autofocusAllFields(split,c,9),focusedZ,1e-8,"Splitting spectral weights preserves focus");
    auto mono=lens;mono.fields.resize(1);mono.wavelengths={{.55,1}};mono.primary=0;
    auto legacyFocus=mono;autofocus(legacyFocus,c);autofocusAllFields(mono,c,13);
    auto weightedMetric=metric;weightedMetric.pupilGrid=13;
    check(evaluateMerit(mono,c,weightedMetric).score<=evaluateMerit(legacyFocus,c,weightedMetric).score+1e-13,
          "Common focus minimizes power-weighted RMS at least as well as legacy unweighted focus");
    auto invalid=lens;invalid.fieldType=FieldType::RealImageHeight;
    rejects([&]{autofocusAllFields(invalid,c,9);},"Focus rejects a field definition dependent on the image plane");
    near(invalid.imageZ(),lens.imageZ(),0,"Rejected common focus is transactional");
    auto wavePlan=metric;wavePlan.operands={{MeritKind::WavefrontRMS,-1,0,.001,2}};
    const auto wave=evaluateMerit(lens,c,wavePlan);
    double expectedSquared=0,mean=0;
    for(size_t f=0;f<lens.fields.size();++f)for(size_t w=0;w<lens.wavelengths.size();++w) {
        auto a=lens;a.primary=w;const auto rms=wavefront(a,c,lens.fields[f],9).rms;
        const double weight=lens.fields[f].weight/4*lens.wavelengths[w].weight/7;
        expectedSquared+=weight*rms*rms/1e-6;mean+=weight*rms;
    }
    near(wave.score*wave.score,expectedSquared,1e-10,"Wavefront merit weights separate squared wave/field residuals");
    near(wave.values[0],mean,1e-12,"Wavefront merit reports weighted mean in millimetres");
    near(std::inner_product(wave.residuals.begin(),wave.residuals.end(),wave.residuals.begin(),0.),wave.score*wave.score,1e-10,"Residual vector norm equals reported merit");
    check(wave.residuals.size()==6,"Wavefront retains every field/wavelength residual");
    auto heights=lens;heights.fieldType=FieldType::ParaxialImageHeight;heights.fields={{0,0,1},{1,2,3}};
    auto sameObject=heights;sameObject.fieldType=FieldType::Angle;
    for(auto& field:sameObject.fields)field=angularField(heights,c,field);
    near(evaluateMerit(heights,c,wavePlan).score,evaluateMerit(sameObject,c,wavePlan).score,1e-10,
         "Spectral OPD keeps the same physical object when the field is specified as image height");
    auto geometryPlan=metric;geometryPlan.operands={{MeritKind::EdgeThickness,-1,0,1,1}};
    geometryPlan.operands[0].height=5;
    const double edge=5-2*(50-std::sqrt(2500-25));
    near(evaluateMerit(lens,c,geometryPlan).values[0],edge,1e-12,"Edge criterion matches independent symmetric-sphere sag formula");
    geometryPlan.operands[0].height=500;
    rejects([&]{evaluateMerit(lens,c,geometryPlan);},"Edge outside surface aperture is rejected");
    auto distortionPlan=metric;distortionPlan.operands={{MeritKind::Distortion,0,0,1,1}};
    near(evaluateMerit(lens,c,distortionPlan).values[0],0,1e-12,"Axis distortion operand is zero");
    auto mtfPlan=metric;mtfPlan.pupilGrid=33;mtfPlan.operands={{MeritKind::MTFX,0,1,1,1},{MeritKind::MTFY,0,1,1,1}};
    for(auto& o:mtfPlan.operands)o.frequency=0;
    near(evaluateMerit(lens,c,mtfPlan).score,0,1e-10,"Polychromatic MTF operands obey unity at zero frequency");
    mtfPlan.operands[0].frequency=1e12;
    rejects([&]{evaluateMerit(lens,c,mtfPlan);},"MTF does not extrapolate outside its physical frequency grid");
    mtfPlan.operands[0].frequency=50;mtfPlan.pupilGrid=3;
    check(!mtfPlan.validate(lens).empty(),"MTF undersampling is rejected");
    // Real defocus task: objective and timing compared at the same pupil grid.
    auto defocused=lens;defocused.fields.resize(1);defocused.wavelengths={{.5875618,1}};defocused.primary=0;
    autofocus(defocused,c);defocused.surfaces.back().thickness+=4;
    auto focusPlan=metric;focusPlan.algorithm=OptimizationAlgorithm::LevenbergMarquardt;
    focusPlan.variables={{VariableParameter::Thickness,1,35,65,1}};
    auto target=defocused;const auto opticalResult=optimize(target,c,focusPlan);
    check(opticalResult.after<opticalResult.before*.2,"DLS improves an actual defocused refracting lens");
    check(target.validate(c).empty(),"DLS optical result remains valid");
    return count;
}
