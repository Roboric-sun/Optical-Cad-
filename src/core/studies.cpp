#include "optics/studies.hpp"
#include "optimization_internal.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <random>
#include <set>

namespace optics {
bool fieldTarget(DesignTarget t){return t.parameter>=DesignParameter::FieldX&&t.parameter<=DesignParameter::FieldWeight;}
bool surfaceTarget(DesignTarget t){return !fieldTarget(t)&&t.parameter!=DesignParameter::Defocus&&t.parameter!=DesignParameter::ObjectDistance&&t.parameter!=DesignParameter::PupilDiameter;}
const char* designParameterName(DesignParameter p){
    static const char* names[]={"Радиус, мм","Толщина, мм","Коника","A4, мм⁻³","A6, мм⁻⁵","A8, мм⁻⁷","A10, мм⁻⁹","Дефокус, мм",
        "Децентрировка X, мм","Децентрировка Y, мм","Децентрировка Z, мм","Наклон X, °","Наклон Y, °","Наклон Z, °","Добавка Δn","Радиус тора Rx, мм",
        "Расстояние объекта, мм","Диаметр зрачка, мм","Поле X","Поле Y","Вес поля","Кривизна, мм⁻¹","XY x², мм⁻¹","XY xy, мм⁻¹","XY y², мм⁻¹","XY x³, мм⁻²","XY x²y, мм⁻²","XY xy², мм⁻²","XY y³, мм⁻²"};
    if(int(p)<0||int(p)>int(DesignParameter::XY03))throw std::invalid_argument("Неизвестный параметр исследования");
    return names[int(p)];
}
namespace {
void targetCheck(const SequentialSystem& s,DesignTarget t){
    designParameterName(t.parameter);
    if((surfaceTarget(t)&&t.index>=s.surfaces.size())||(fieldTarget(t)&&t.index>=s.fields.size())||(!surfaceTarget(t)&&!fieldTarget(t)&&t.index!=0))
        throw std::invalid_argument("Ссылка параметра вне модели");
}
std::pair<int,size_t> key(DesignTarget t){return {int(t.parameter==DesignParameter::Curvature?DesignParameter::Radius:t.parameter),surfaceTarget(t)||fieldTarget(t)?t.index:0};}
void require(const std::vector<std::string>& errors){if(!errors.empty())throw std::invalid_argument(errors.front());}
OptimizationPlan meritPlan(OptimizationPlan p){p.variables={OptimizationVariable{VariableParameter::Defocus,0,-1,1,.1}};p.refocus=false;p.allFieldsFocus=false;return p;}
DesignTarget target(const OptimizationVariable& v){
    if(v.parameter<=VariableParameter::Defocus)return {DesignParameter(int(v.parameter)),v.parameter==VariableParameter::Defocus?0:v.surface};
    throw std::invalid_argument("Совместная оптимизация поддерживает R, толщину, конику, A4…A10 и дефокус; более высокие асферики остаются в обычном DLS");
}
struct Cancelled {};
struct Range {double lower,upper,step;};
struct Search {std::vector<double> x; OptimizationResult result;};
Search search(std::vector<double> x,const std::vector<Range>& ranges,size_t iterations,
    const std::function<double(const std::vector<double>&)>& score,const std::function<bool(size_t,size_t,double)>& progress){
    Search out;out.x=x;auto& r=out.result;const size_t maximum=1+iterations*4*x.size();
    auto evaluate=[&](const std::vector<double>& a){if(progress&&!progress(r.evaluations,maximum,r.after))throw Cancelled{};++r.evaluations;
        try{const double v=score(a);return std::isfinite(v)?v:std::numeric_limits<double>::infinity();}catch(const Cancelled&){throw;}catch(const std::exception&){return std::numeric_limits<double>::infinity();}};
    std::vector<double> steps;for(size_t i=0;i<x.size();++i){x[i]=std::clamp(x[i],ranges[i].lower,ranges[i].upper);steps.push_back(ranges[i].step);}
    try {
        r.before=r.after=evaluate(x);r.history.push_back(r.after);
        for(size_t it=0;it<iterations;++it){bool improved=false;
            for(size_t j=0;j<x.size();++j){const double old=x[j];auto best=x;double bestScore=r.after;
                // Also try the bounds on the first sweep; a blocked nominal beam can recover.
                const std::array<double,4> candidates{old-steps[j],old+steps[j],ranges[j].lower,ranges[j].upper};
                for(size_t k=0;k<(it==0?4:2);++k){auto trial=x;trial[j]=std::clamp(candidates[k],ranges[j].lower,ranges[j].upper);if(trial[j]==old)continue;
                    const double value=evaluate(trial);if(value<bestScore){best=std::move(trial);bestScore=value;}}
                if(bestScore<r.after){x=std::move(best);r.after=bestScore;improved=true;}else steps[j]*=.5;
            }
            r.history.push_back(r.after);if(!improved&&std::all_of(steps.begin(),steps.end(),[](double v){return v<1e-10;})){r.termination="Малый шаг";break;}
        }
        if(!std::isfinite(r.after))throw std::runtime_error("Не найдено допустимое состояние");
        if(progress&&!progress(r.evaluations,maximum,r.after))throw Cancelled{};
        if(r.termination.empty())r.termination="Лимит итераций координатного поиска";
        out.x=x;
    }catch(const Cancelled&){r.cancelled=true;r.termination="Отмена";}
    return out;
}
std::vector<Configuration> states(const std::vector<Configuration>& configs){return configs.empty()?std::vector<Configuration>{Configuration{"База",1,{},{}}}:configs;}
std::vector<std::vector<double>> factor(const TolerancePlan& p){
    const size_t n=p.operands.size();auto a=p.correlation;
    if(a.empty()){a.assign(n,std::vector<double>(n));for(size_t i=0;i<n;++i)a[i][i]=1;}
    if(a.size()!=n)throw std::invalid_argument("Размер матрицы корреляции не соответствует допускам");
    for(const auto& row:a)if(row.size()!=n)throw std::invalid_argument("Матрица корреляции должна быть квадратной");
    for(size_t i=0;i<n;++i)for(size_t j=0;j<n;++j)
        if(!std::isfinite(a[i][j])||std::abs(a[i][j])>1||std::abs(a[i][j]-a[j][i])>1e-12||(i==j&&std::abs(a[i][j]-1)>1e-12))
            throw std::invalid_argument("Корреляции: симметрия, диагональ 1, элементы от −1 до 1");
    std::vector<std::vector<double>> l(n,std::vector<double>(n));
    for(size_t i=0;i<n;++i)for(size_t j=0;j<=i;++j){double v=a[i][j];for(size_t k=0;k<j;++k)v-=l[i][k]*l[j][k];
        if(i==j){if(v<-1e-10)throw std::invalid_argument("Матрица корреляции не положительно полуопределена");l[i][j]=std::sqrt(std::max(0.,v));}
        else if(l[j][j]>1e-12)l[i][j]=v/l[j][j];else if(std::abs(v)>1e-10)throw std::invalid_argument("Вырожденная несовместимая корреляция");}
    return l;
}
}
double designValue(const SequentialSystem& s,const Catalog& c,DesignTarget t){
    targetCheck(s,t);
    if(fieldTarget(t)){const auto& f=s.fields[t.index];return t.parameter==DesignParameter::FieldX?f.x:t.parameter==DesignParameter::FieldY?f.y:f.weight;}
    if(t.parameter==DesignParameter::Defocus)return s.defocus;
    if(t.parameter==DesignParameter::ObjectDistance)return s.objectDistance;
    if(t.parameter==DesignParameter::PupilDiameter)return s.pupilDiameter;
    const auto& a=s.surfaces[t.index];
    if(t.parameter>=DesignParameter::XY20)return a.xySag[int(t.parameter)-int(DesignParameter::XY20)];
    if(t.parameter>=DesignParameter::A4&&t.parameter<=DesignParameter::A10)return a.asphere[int(t.parameter)-int(DesignParameter::A4)];
    switch(t.parameter){
    case DesignParameter::Radius:return a.radius;case DesignParameter::Curvature:return a.radius==0?0:1/a.radius;
    case DesignParameter::Thickness:return a.thickness;case DesignParameter::Conic:return a.conic;
    case DesignParameter::DecenterX:return a.decenter.x;case DesignParameter::DecenterY:return a.decenter.y;case DesignParameter::DecenterZ:return a.decenter.z;
    case DesignParameter::TiltX:return a.tilt.x;case DesignParameter::TiltY:return a.tilt.y;case DesignParameter::TiltZ:return a.tilt.z;
    case DesignParameter::IndexOffset:return c.get(a.material).indexOffset;
    case DesignParameter::RotationRadius:return a.rotationRadius;
    default:throw std::invalid_argument("Параметр не поддерживается");}
}
void setDesignValue(SequentialSystem& s,Catalog& c,DesignTarget t,double value){
    targetCheck(s,t);if(!std::isfinite(value))throw std::invalid_argument("Значение должно быть конечным");
    if(fieldTarget(t)){auto& f=s.fields[t.index];if(t.parameter==DesignParameter::FieldX)f.x=value;else if(t.parameter==DesignParameter::FieldY)f.y=value;else f.weight=value;return;}
    if(t.parameter==DesignParameter::Defocus){s.defocus=value;return;}
    if(t.parameter==DesignParameter::ObjectDistance){s.objectDistance=value;return;}
    if(t.parameter==DesignParameter::PupilDiameter){s.pupilDiameter=value;return;}
    auto& a=s.surfaces[t.index];
    if(t.parameter>=DesignParameter::XY20){a.xySag[int(t.parameter)-int(DesignParameter::XY20)]=value;return;}
    if(t.parameter>=DesignParameter::A4&&t.parameter<=DesignParameter::A10){a.asphere[int(t.parameter)-int(DesignParameter::A4)]=value;return;}
    switch(t.parameter){
    case DesignParameter::Radius:a.radius=value;break;case DesignParameter::Curvature:a.radius=value==0?0:1/value;break;
    case DesignParameter::Thickness:a.thickness=value;break;case DesignParameter::Conic:a.conic=value;break;
    case DesignParameter::DecenterX:a.decenter.x=value;break;case DesignParameter::DecenterY:a.decenter.y=value;break;case DesignParameter::DecenterZ:a.decenter.z=value;break;
    case DesignParameter::TiltX:a.tilt.x=value;break;case DesignParameter::TiltY:a.tilt.y=value;break;case DesignParameter::TiltZ:a.tilt.z=value;break;
    case DesignParameter::RotationRadius:a.rotationRadius=value;break;
    case DesignParameter::IndexOffset:{auto m=c.get(a.material);m.indexOffset=value;const auto prefix="__study_S"+std::to_string(t.index+1)+"_";
        std::string name=prefix+m.name;while(std::any_of(c.materials.begin(),c.materials.end(),[&](const auto& v){return v.name==name;}))name="_"+name;
        m.name=name;c.add(m);a.material=name;break;}
    default:throw std::invalid_argument("Параметр не поддерживается");}
}
OpticalDesign configurationDesign(const OpticalDesign& base,const Configuration& config){
    auto out=base;
    for(const auto& m:config.materials){out.catalog.get(m.material);out.system.surfaces.at(m.surface).material=m.material;}
    for(const auto& v:config.values){targetCheck(out.system,v.target);const auto p=v.target.parameter;
        if(surfaceTarget(v.target)&&((p==DesignParameter::Radius||p==DesignParameter::Curvature)&&isSolved(base.system,SolveParameter::Radius,v.target.index)))throw std::invalid_argument("Конфигурация не может подменить связанный радиус");
        if(p==DesignParameter::Thickness&&isSolved(base.system,SolveParameter::Thickness,v.target.index))throw std::invalid_argument("Конфигурация не может подменить связанную толщину");
        setDesignValue(out.system,out.catalog,v.target,v.value);}
    applySolves(out.system,out.catalog);require(out.system.validate(out.catalog));return out;
}
std::vector<std::string> validateConfigurations(const OpticalDesign& base,const std::vector<Configuration>& configs){
    try{
        if(configs.size()>32)throw std::invalid_argument("Не более 32 конфигураций");
        std::set<std::string> names;
        for(const auto& c:configs){if(c.name.empty()||c.name.size()>300||!names.insert(c.name).second||!std::isfinite(c.weight)||c.weight<=0||c.weight>1e12||c.values.size()>128||c.materials.size()>500)throw std::invalid_argument("Имя, вес или размер конфигурации неверны");
            std::set<std::pair<int,size_t>> keys;std::set<size_t> materials;
            for(const auto& v:c.values){targetCheck(base.system,v.target);if(!std::isfinite(v.value)||!keys.insert(key(v.target)).second)throw std::invalid_argument("Повтор или неверное значение параметра конфигурации");
                if(!std::isfinite(v.lower)||!std::isfinite(v.upper)||!std::isfinite(v.step)||v.lower>=v.upper||!std::isfinite(v.upper-v.lower)||v.step<=0||v.step>v.upper-v.lower||(v.variable&&(v.value<v.lower||v.value>v.upper)))throw std::invalid_argument("Некорректные границы переменной конфигурации");}
            for(const auto& m:c.materials)if(!materials.insert(m.surface).second)throw std::invalid_argument("Материал поверхности задан дважды");
            configurationDesign(base,c);
        }
        return {};
    }catch(const std::exception& e){return {e.what()};}
}
ConfigurationEvaluation evaluateConfigurations(const OpticalDesign& base,const std::vector<Configuration>& configs,const OptimizationPlan& p){
    require(validateConfigurations(base,configs));ConfigurationEvaluation out;double weights=0,sum=0;
    for(const auto& config:states(configs)){const auto d=configurationDesign(base,config);const double score=evaluateMerit(d.system,d.catalog,meritPlan(p)).score;out.scores.push_back(score);weights+=config.weight;sum+=config.weight*score*score;}
    out.score=std::sqrt(sum/weights);if(!std::isfinite(out.score))throw std::runtime_error("Переполнение общей функции качества");return out;
}
OptimizationResult optimizeConfigurations(OpticalDesign& base,std::vector<Configuration>& configs,const OptimizationPlan& p,std::function<bool(size_t,size_t,double)> progress){
    require(validateConfigurations(base,configs));require(p.validate(base.system));
    if(p.refocus||p.starts!=1)throw std::invalid_argument("Для общей оптимизации задайте явные дефокусы, выключите автофокус и выберите один старт");
    struct Binding {int config;size_t row;DesignTarget target;};std::vector<Binding> bindings;std::vector<Range> ranges;std::vector<double> x;
    std::set<std::pair<int,size_t>> shared;
    for(const auto& v:p.variables){const auto t=target(v);shared.insert(key(t));bindings.push_back({-1,0,t});ranges.push_back({v.lower,v.upper,v.step});x.push_back(designValue(base.system,base.catalog,t));}
    for(size_t i=0;i<configs.size();++i)for(size_t j=0;j<configs[i].values.size();++j){const auto& v=configs[i].values[j];if(shared.contains(key(v.target)))throw std::invalid_argument("Общая переменная перекрыта значением конфигурации: уберите конфликт");
        if(v.variable){bindings.push_back({int(i),j,v.target});ranges.push_back({v.lower,v.upper,v.step});x.push_back(v.value);}}
    if(bindings.size()>128)throw std::invalid_argument("Не более 128 общих и локальных переменных");
    auto build=[&](const auto& values){auto d=base;auto cs=configs;for(size_t i=0;i<bindings.size();++i){const auto b=bindings[i];if(b.config<0)setDesignValue(d.system,d.catalog,b.target,values[i]);else cs[b.config].values[b.row].value=values[i];}return std::make_pair(std::move(d),std::move(cs));};
    auto found=search(x,ranges,p.iterations,[&](const auto& values){auto [d,cs]=build(values);return evaluateConfigurations(d,cs,p).score;},progress);
    if(!found.result.cancelled){auto [d,cs]=build(found.x);applySolves(d.system,d.catalog);base=std::move(d);configs=std::move(cs);}return found.result;
}
std::vector<std::string> TolerancePlan::validate(const OpticalDesign& base,const std::vector<Configuration>& configs)const{
    try{
        require(validateConfigurations(base,configs));if(operands.empty()||operands.size()>64||compensators.size()>8||samples<1||samples>10000||compensationIterations<1||compensationIterations>50||!std::isfinite(limit)||limit<0)throw std::invalid_argument("Допуски: 1…64 строк, до 8 компенсаторов, 1…10000 образцов, конечный предел ≥ 0");
        std::set<std::pair<int,size_t>> seen;
        for(const auto& t:operands){targetCheck(base.system,t.target);if(fieldTarget(t.target)||t.target.parameter==DesignParameter::ObjectDistance||t.target.parameter==DesignParameter::PupilDiameter||!seen.insert(key(t.target)).second||!std::isfinite(t.amplitude)||t.amplitude<0||!std::isfinite(t.mean)||int(t.distribution)<0||int(t.distribution)>1)throw std::invalid_argument("Некорректный или повторный производственный допуск");}
        seen.clear();for(const auto& t:compensators){targetCheck(base.system,t.target);if(fieldTarget(t.target)||t.target.parameter==DesignParameter::ObjectDistance||t.target.parameter==DesignParameter::PupilDiameter||!seen.insert(key(t.target)).second||!std::isfinite(t.lower)||!std::isfinite(t.upper)||t.lower>0||t.upper<0||t.lower>=t.upper||!std::isfinite(t.upper-t.lower)||!std::isfinite(t.step)||t.step<=0||t.step>t.upper-t.lower)throw std::invalid_argument("Границы компенсатора должны включать ноль, шаг > 0; повторы запрещены");}
        for(const auto& config:states(configs)){const auto d=configurationDesign(base,config);auto supported=[&](DesignTarget t){if(!surfaceTarget(t))return;const auto& a=d.system.surfaces.at(t.index);if(t.parameter==DesignParameter::RotationRadius&&a.profile!=SurfaceProfile::Toroidal)throw std::invalid_argument("Rx допуска определён только для тора");if(t.parameter>=DesignParameter::XY20&&a.profile!=SurfaceProfile::XYPolynomial)throw std::invalid_argument("XY коэффициент допуска требует XY поверхность");if(t.parameter>=DesignParameter::A4&&t.parameter<=DesignParameter::A10&&a.profile!=SurfaceProfile::Standard)throw std::invalid_argument("A4…A10 допуска требуют стандартную асферику");if(t.parameter==DesignParameter::Conic&&(a.profile==SurfaceProfile::Toroidal||a.profile==SurfaceProfile::XYPolynomial))throw std::invalid_argument("Этот профиль не поддерживает допуск коники");if((t.parameter==DesignParameter::Radius&&a.radius==0)||((t.parameter==DesignParameter::Radius||t.parameter==DesignParameter::Curvature)&&a.profile==SurfaceProfile::XYPolynomial))throw std::invalid_argument("Для плоскости используйте допуск кривизны; XY имеет отдельные коэффициенты");};for(const auto& t:operands)supported(t.target);for(const auto& t:compensators)supported(t.target);}
        factor(*this);return {};
    }catch(const std::exception& e){return {e.what()};}
}
YieldEstimate wilsonYield(size_t accepted,size_t total){
    if(accepted>total)throw std::invalid_argument("Число годных превышает число испытаний");
    YieldEstimate out;out.accepted=accepted;out.total=total;if(!total)return out;
    const double n=double(total),p=double(accepted)/n,z=1.959963984540054,z2=z*z,den=1+z2/n;
    const double center=(p+z2/(2*n))/den,half=z*std::sqrt(p*(1-p)/n+z2/(4*n*n))/den;
    out.fraction=p;out.lower=std::max(0.,center-half);out.upper=std::min(1.,center+half);return out;
}
std::vector<std::vector<double>> toleranceDraws(const TolerancePlan& p,size_t count){
    if(p.operands.size()>64||count>10000)throw std::invalid_argument("Слишком большая выборка");
    const auto l=factor(p);std::mt19937_64 random(p.seed);const size_t n=p.operands.size();std::vector<std::vector<double>> out;
    auto uniform=[&]{return (double(random()>>11)+.5)/9007199254740992.;};
    for(size_t k=0;k<count;++k){std::vector<double> z(n),row(n);for(auto& v:z)v=std::sqrt(-2*std::log(uniform()))*std::cos(2*3.14159265358979323846*uniform());
        for(size_t i=0;i<n;++i){double v=0;for(size_t j=0;j<=i;++j)v+=l[i][j]*z[j];const auto t=p.operands[i];
            if(!std::isfinite(t.amplitude)||t.amplitude<0||!std::isfinite(t.mean)||int(t.distribution)<0||int(t.distribution)>1)throw std::invalid_argument("Некорректное распределение допуска");
            row[i]=t.mean+t.amplitude*(t.distribution==ToleranceDistribution::Normal?v/3:std::erf(v/std::sqrt(2.)));}
        out.push_back(std::move(row));}return out;
}
OpticalDesign toleranceRealization(const OpticalDesign& base,const Configuration& config,const TolerancePlan& plan,const ToleranceSample& sample,bool compensated){
    if(sample.deltas.size()!=plan.operands.size()||(compensated&&sample.corrections.size()!=plan.compensators.size()))throw std::invalid_argument("Образец не соответствует плану допусков");
    auto out=configurationDesign(base,config);out.system=resolvedSystem(out.system,out.catalog);
    // Resolve first: manufacturing errors must survive the nominal pickup constraints.
    for(size_t i=0;i<sample.deltas.size();++i){auto t=plan.operands[i].target;setDesignValue(out.system,out.catalog,t,designValue(out.system,out.catalog,t)+sample.deltas[i]);}
    if(compensated)for(size_t i=0;i<sample.corrections.size();++i){const auto& c=plan.compensators[i];const double v=sample.corrections[i];if(v<c.lower-1e-12||v>c.upper+1e-12)throw std::invalid_argument("Коррекция вышла за границы");setDesignValue(out.system,out.catalog,c.target,designValue(out.system,out.catalog,c.target)+v);}
    require(out.system.validate(out.catalog));return out;
}
ToleranceReport runTolerances(const OpticalDesign& base,const std::vector<Configuration>& configs,const OptimizationPlan& p,const TolerancePlan& plan,std::function<bool(size_t,size_t,double)> progress){
    require(plan.validate(base,configs));
    auto alignment=[](DesignTarget t){return t.parameter>=DesignParameter::DecenterX&&t.parameter<=DesignParameter::TiltZ;};
    const bool align=std::any_of(plan.operands.begin(),plan.operands.end(),[&](const auto& t){return alignment(t.target)&&(t.mean!=0||t.amplitude!=0);})||std::any_of(plan.compensators.begin(),plan.compensators.end(),[&](const auto& t){return alignment(t.target);});
    if(align)for(const auto& o:p.operands)if(o.kind!=MeritKind::SpotRMS&&o.kind!=MeritKind::CentroidX&&o.kind!=MeritKind::CentroidY&&o.kind!=MeritKind::Throughput&&o.kind!=MeritKind::ImageDistance)throw std::invalid_argument("Допуски юстировки требуют геометрической функции качества: пятно, центр, пропускание или положение изображения");
    const auto all=states(configs);const auto quality=meritPlan(p);ToleranceReport out;
    auto poll=[&](size_t done){if(progress&&!progress(done,plan.samples+2*plan.operands.size(),out.nominal))throw Cancelled{};};
    size_t done=0;
    auto score=[&](const ToleranceSample& sample,bool compensated){poll(done);double worst=0;for(const auto& config:all){const auto d=toleranceRealization(base,config,plan,sample,compensated);worst=std::max(worst,evaluateMerit(d.system,d.catalog,quality).score);}return worst;};
    auto evaluate=[&](const std::vector<double>& deltas){ToleranceSample sample;sample.deltas=deltas;sample.corrections.assign(plan.compensators.size(),0);
        try{sample.before=score(sample,false);}catch(const Cancelled&){throw;}catch(const std::exception& e){sample.beforeError=e.what();}
        sample.after=sample.before;sample.afterError=sample.beforeError;
        if(!plan.compensators.empty()){
            std::vector<Range> bounds;for(const auto& c:plan.compensators)bounds.push_back({c.lower,c.upper,c.step});
            try{auto found=search(sample.corrections,bounds,plan.compensationIterations,[&](const auto& x){auto trial=sample;trial.corrections=x;return score(trial,true);},[&](size_t,size_t,double){poll(done);return true;});
                if(found.result.cancelled)throw Cancelled{};sample.corrections=found.x;sample.after=score(sample,true);sample.afterError.clear();
            }catch(const Cancelled&){throw;}catch(const std::exception& e){sample.after.reset();sample.afterError=e.what();}}
        return sample;};
    try{
        ToleranceSample nominal;nominal.deltas.assign(plan.operands.size(),0);out.nominal=score(nominal,false);
        for(size_t i=0;i<plan.operands.size();++i){std::vector<double> d(plan.operands.size());d[i]=-plan.operands[i].amplitude;
            ToleranceSensitivity sensitivity;sensitivity.operand=i;sensitivity.minus=evaluate(d);++done;d[i]=-d[i];sensitivity.plus=evaluate(d);++done;
            sensitivity.impact=(!sensitivity.minus.after||!sensitivity.plus.after)?std::numeric_limits<double>::infinity():std::max(std::abs(*sensitivity.minus.after-out.nominal),std::abs(*sensitivity.plus.after-out.nominal));out.sensitivities.push_back(std::move(sensitivity));}
        const auto draws=toleranceDraws(plan,plan.samples);size_t before=0,after=0;
        for(const auto& d:draws){poll(done);auto sample=evaluate(d);++done;if(sample.before&&*sample.before<=plan.limit)++before;if(sample.after&&*sample.after<=plan.limit)++after;out.samples.push_back(std::move(sample));
            const auto n=out.samples.size();if((n>=16&&(n&(n-1))==0)||n==plan.samples)out.convergence.push_back(wilsonYield(after,n));}
        out.before=wilsonYield(before,out.samples.size());out.after=wilsonYield(after,out.samples.size());poll(done);
    }catch(const Cancelled&){out.cancelled=true;}
    return out;
}
} // namespace optics
