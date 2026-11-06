#include "automation.hpp"
#include <QJsonArray>
#include <QJsonDocument>
using namespace optics;
std::optional<QJsonObject> executeStudyRequest(Project& p,const QString& operation,const QJsonObject& options){
 if(operation!="analyze_configurations"&&operation!="optimize_configurations"&&operation!="tolerances"&&operation!="tolerance_sample")return {};
 auto plan=p.optimization?*p.optimization:defaultOptimization(p.system,p.catalog);
 if(operation=="analyze_configurations"){
    const auto e=evaluateConfigurations({p.system,p.catalog},p.configurations,plan);QJsonArray states;
    for(size_t i=0;i<e.scores.size();++i){auto state=p.configurations.empty()?p:configurationProject(p,i);const auto request=QJsonDocument::fromJson(serializeProject(state)).object();
        auto analysis=executeRequest({{"operation","analyze"},{"project",request},{"options",QJsonObject{{"pupil_grid",plan.pupilGrid}}}});
        states.append(QJsonObject{{"name",p.configurations.empty()?"База":QString::fromStdString(p.configurations[i].name)},{"weight",p.configurations.empty()?1:p.configurations[i].weight},{"score",e.scores[i]},{"analysis",analysis}});}
    return QJsonObject{{"score",e.score},{"states",states},{"aggregation","weighted quadratic"}};
 }
 if(operation=="optimize_configurations"){
    OpticalDesign d{p.system,p.catalog};const auto r=optimizeConfigurations(d,p.configurations,plan);p.system=d.system;p.catalog=d.catalog;
    return QJsonObject{{"before",r.before},{"after",r.after},{"evaluations",double(r.evaluations)},{"algorithm","bounded coordinate"},{"project",QJsonDocument::fromJson(serializeProject(p)).object()}};
 }
 if(operation=="tolerances"){
    if(!p.tolerance)throw std::invalid_argument("Нет плана допусков");auto result=toleranceReportJson(runTolerances({p.system,p.catalog},p.configurations,plan,*p.tolerance));result["seed"]=QString::number(p.tolerance->seed);return result;
 }
 if(operation=="tolerance_sample"){
    auto read=[&](const char* key){if(!options[key].isArray()||options[key].toArray().size()>64)throw std::invalid_argument("Образец требует массивы deltas и corrections");std::vector<double> v;for(const auto& j:options[key].toArray()){if(!j.isDouble()||!std::isfinite(j.toDouble()))throw std::invalid_argument("Некорректное значение образца");v.push_back(j.toDouble());}return v;};
    ToleranceSample sample;sample.deltas=read("deltas");sample.corrections=read("corrections");size_t state=0;if(options.contains("state")){const auto v=options["state"];if(!v.isDouble()||v.toDouble()<0||v.toDouble()>31||std::floor(v.toDouble())!=v.toDouble())throw std::invalid_argument("Некорректное состояние образца");state=size_t(v.toDouble());}
    auto realized=realizedProject(p,sample,state);return QJsonObject{{"sample_project",QJsonDocument::fromJson(serializeProject(realized)).object()}};
 }
 return {};
}
