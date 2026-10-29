#include "project.hpp"
#include <QJsonArray>
#include <algorithm>
using namespace optics;
namespace {
double number(const QJsonObject& o,const char* k){const auto v=o[k];if(!v.isDouble()||!std::isfinite(v.toDouble()))throw std::invalid_argument(std::string("Некорректное число исследования: ")+k);return v.toDouble();}
size_t integer(const QJsonObject& o,const char* k,size_t max){double v=number(o,k);if(v<0||v>double(max)||std::floor(v)!=v)throw std::invalid_argument(std::string("Некорректный индекс исследования: ")+k);return size_t(v);}
QJsonArray array(const QJsonObject& o,const char* k,int max){if(!o[k].isArray()||o[k].toArray().size()>max)throw std::invalid_argument(std::string("Некорректная таблица исследования: ")+k);return o[k].toArray();}
QJsonObject object(const QJsonValue& v){if(!v.isObject())throw std::invalid_argument("Ожидается запись исследования");return v.toObject();}
DesignTarget target(const QJsonObject& o){return {DesignParameter(integer(o,"parameter",int(DesignParameter::XY03))),integer(o,"index",499)};}
QJsonObject row(DesignTarget t){return {{"parameter",int(t.parameter)},{"index",double(t.index)}};}
QJsonArray vector(const std::vector<double>& a){QJsonArray out;for(auto v:a)out.append(v);return out;}
QJsonObject sample(const ToleranceSample& s){return {{"deltas",vector(s.deltas)},{"corrections",vector(s.corrections)},{"before",s.before?QJsonValue(*s.before):QJsonValue()},{"after",s.after?QJsonValue(*s.after):QJsonValue()},
    {"before_error",QString::fromStdString(s.beforeError)},{"after_error",QString::fromStdString(s.afterError)}};}
QJsonObject yield(const YieldEstimate& y){return {{"accepted",double(y.accepted)},{"total",double(y.total)},{"fraction",y.fraction},{"lower95",y.lower},{"upper95",y.upper}};}
}
void writeStudies(QJsonObject& root,const Project& p){
    if(!p.configurations.empty()){
        QJsonArray configs;for(const auto& c:p.configurations){QJsonArray values,materials;
            for(const auto& v:c.values){auto j=row(v.target);j["value"]=v.value;j["variable"]=v.variable;j["lower"]=v.lower;j["upper"]=v.upper;j["step"]=v.step;values.append(j);}
            for(const auto& m:c.materials)materials.append(QJsonObject{{"surface",double(m.surface)},{"material",QString::fromStdString(m.material)}});
            configs.append(QJsonObject{{"name",QString::fromStdString(c.name)},{"weight",c.weight},{"values",values},{"materials",materials}});}
        root["configurations"]=configs;root["version"]=std::max(root["version"].toInt(),11);
    }
    if(p.tolerance){const auto& t=*p.tolerance;QJsonArray operands,compensators,correlations;
        for(const auto& v:t.operands){auto j=row(v.target);j["amplitude"]=v.amplitude;j["mean"]=v.mean;j["distribution"]=int(v.distribution);operands.append(j);}
        for(const auto& v:t.compensators){auto j=row(v.target);j["lower"]=v.lower;j["upper"]=v.upper;j["step"]=v.step;compensators.append(j);}
        for(const auto& r:t.correlation)correlations.append(vector(r));
        root["tolerance"]=QJsonObject{{"operands",operands},{"compensators",compensators},{"correlation",correlations},{"samples",double(t.samples)},{"compensationIterations",double(t.compensationIterations)},{"seed",QString::number(t.seed)},{"limit",t.limit}};
        root["version"]=std::max(root["version"].toInt(),12);
    }
}
void readStudies(const QJsonObject& root,Project& p,bool validate){
    if(root.contains("configurations")){
        if(root["version"].toInt()<11)throw std::invalid_argument("Конфигурации требуют формат 11");
        for(const auto& record:array(root,"configurations",32)){auto c=object(record);Configuration config;
            if(!c["name"].isString())throw std::invalid_argument("Отсутствует имя конфигурации");config.name=c["name"].toString().toStdString();config.weight=number(c,"weight");
            for(const auto& record:array(c,"values",128)){auto j=object(record);if(!j["variable"].isBool())throw std::invalid_argument("Флаг переменной должен быть boolean");config.values.push_back({target(j),number(j,"value"),j["variable"].toBool(),number(j,"lower"),number(j,"upper"),number(j,"step")});}
            for(const auto& record:array(c,"materials",500)){auto j=object(record);if(!j["material"].isString())throw std::invalid_argument("Материал должен быть строкой");config.materials.push_back({integer(j,"surface",499),j["material"].toString().toStdString()});}
            p.configurations.push_back(std::move(config));}
    }
    if(root.contains("tolerance")){
        if(root["version"].toInt()<12)throw std::invalid_argument("Допуски требуют формат 12");
        const auto j=object(root["tolerance"]);TolerancePlan t;
        for(const auto& record:array(j,"operands",64)){auto v=object(record);t.operands.push_back({target(v),number(v,"amplitude"),number(v,"mean"),ToleranceDistribution(integer(v,"distribution",1))});}
        for(const auto& record:array(j,"compensators",8)){auto v=object(record);t.compensators.push_back({target(v),number(v,"lower"),number(v,"upper"),number(v,"step")});}
        for(const auto& record:array(j,"correlation",64)){if(!record.isArray()||record.toArray().size()>64)throw std::invalid_argument("Некорректная строка корреляций");std::vector<double> values;for(const auto& v:record.toArray()){if(!v.isDouble()||!std::isfinite(v.toDouble()))throw std::invalid_argument("Некорректная корреляция");values.push_back(v.toDouble());}t.correlation.push_back(values);}
        t.samples=integer(j,"samples",10000);t.compensationIterations=integer(j,"compensationIterations",50);t.limit=number(j,"limit");
        bool ok=false;if(j["seed"].isString())t.seed=j["seed"].toString().toULongLong(&ok);if(!ok)throw std::invalid_argument("Некорректный seed допусков");p.tolerance=t;
    }
    if(validate){const OpticalDesign base{p.system,p.catalog};auto errors=validateConfigurations(base,p.configurations);if(!errors.empty())throw std::invalid_argument(errors.front());if(p.tolerance){errors=p.tolerance->validate(base,p.configurations);if(!errors.empty())throw std::invalid_argument(errors.front());}}
}
void reindexStudies(Project& p,const std::vector<size_t>& map){
    auto removed=[&](DesignTarget t){return surfaceTarget(t)&&(t.index>=map.size()||map[t.index]==SIZE_MAX);};
    for(auto& c:p.configurations){std::erase_if(c.values,[&](const auto& v){return removed(v.target);});for(auto& v:c.values)if(surfaceTarget(v.target))v.target.index=map.at(v.target.index);
        std::erase_if(c.materials,[&](const auto& m){return m.surface>=map.size()||map[m.surface]==SIZE_MAX;});for(auto& m:c.materials)m.surface=map.at(m.surface);}
    if(!p.tolerance)return;auto& t=*p.tolerance;std::vector<size_t> kept;for(size_t i=0;i<t.operands.size();++i)if(!removed(t.operands[i].target))kept.push_back(i);
    if(!t.correlation.empty()){std::vector<std::vector<double>> matrix;for(auto i:kept){std::vector<double> row;for(auto j:kept)row.push_back(t.correlation.at(i).at(j));matrix.push_back(row);}t.correlation=matrix;}
    std::erase_if(t.operands,[&](const auto& v){return removed(v.target);});std::erase_if(t.compensators,[&](const auto& v){return removed(v.target);});
    for(auto& v:t.operands)if(surfaceTarget(v.target))v.target.index=map.at(v.target.index);for(auto& v:t.compensators)if(surfaceTarget(v.target))v.target.index=map.at(v.target.index);
    if(t.operands.empty())p.tolerance.reset();
}
Project configurationProject(const Project& p,size_t index){
    const auto& c=p.configurations.at(index);const auto d=configurationDesign({p.system,p.catalog},c);auto out=p;out.system=d.system;out.catalog=d.catalog;out.system.name+=" · "+c.name;out.configurations.clear();out.tolerance.reset();out.workspace["configurationOrigin"]=QString::fromStdString(c.name);return out;
}
Project realizedProject(const Project& p,const ToleranceSample& sample,size_t configuration,bool compensated){
    if(!p.tolerance)throw std::invalid_argument("Нет плана допусков");if(p.configurations.empty()&&configuration!=0)throw std::invalid_argument("У базового проекта только одно состояние");Configuration c;if(!p.configurations.empty())c=p.configurations.at(configuration);const auto d=toleranceRealization({p.system,p.catalog},c,*p.tolerance,sample,compensated);
    auto out=p;out.system=d.system;out.catalog=d.catalog;out.system.name+=" · образец допуска";out.optimization.reset();out.configurations.clear();out.tolerance.reset();out.workspace["toleranceRealization"]=QJsonObject{{"seed",QString::number(p.tolerance->seed)},{"configuration",double(configuration)},{"compensated",compensated},{"values",::sample(sample)}};return out;
}
QJsonObject toleranceReportJson(const ToleranceReport& r){
    QJsonArray samples,sensitivities,convergence;for(const auto& v:r.samples)samples.append(sample(v));for(const auto& v:r.sensitivities)sensitivities.append(QJsonObject{{"operand",double(v.operand)},{"minus",sample(v.minus)},{"plus",sample(v.plus)},{"impact",std::isfinite(v.impact)?QJsonValue(v.impact):QJsonValue()}});for(const auto& y:r.convergence)convergence.append(yield(y));
    return {{"nominal",r.nominal},{"cancelled",r.cancelled},{"before",yield(r.before)},{"after",yield(r.after)},{"sensitivities",sensitivities},{"samples",samples},{"convergence",convergence},{"confidence_method","Wilson 95%"},{"state_aggregation","maximum"}};
}
Project conjugatesProject(){
    Project p;p.system.name="Три сопряжения · конфигурации и допуски";p.system.pupilDiameter=3;p.system.fields={{0,0,1},{0,2,1}};p.system.pupilGrid=9;
    for(auto distance:{0.,500.,250.}){auto d=p.system;d.objectDistance=distance;autofocusAllFields(d,p.catalog,9);Configuration c;c.name=distance==0?"Бесконечность":std::to_string(int(distance))+" мм";
        c.values={{{DesignParameter::ObjectDistance,0},distance,false,0,1000,10},{{DesignParameter::Defocus,0},d.imageZ()-p.system.imageZ(),true,d.imageZ()-p.system.imageZ()-2,d.imageZ()-p.system.imageZ()+2,.2}};p.configurations.push_back(c);}
    OptimizationPlan merit;merit.variables={{VariableParameter::Radius,0,40,60,2}};merit.operands={{MeritKind::SpotRMS,-1,0,.01,1}};merit.minimumThroughput=.8;merit.pupilGrid=9;merit.iterations=20;p.optimization=merit;
    TolerancePlan t;t.operands={{{DesignParameter::Radius,0},.05,0,ToleranceDistribution::Normal},{{DesignParameter::Thickness,0},.02,0,ToleranceDistribution::Uniform},{{DesignParameter::IndexOffset,0},.0001,0,ToleranceDistribution::Normal}};t.compensators={{{DesignParameter::Defocus,0},-.5,.5,.05}};t.limit=1;t.samples=64;p.tolerance=t;return p;
}
