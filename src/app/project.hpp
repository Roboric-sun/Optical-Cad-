#pragma once
#include "optics/studies.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QString>

struct Project {
    optics::Catalog catalog;
    optics::SequentialSystem system = optics::SequentialSystem::demo();
    optics::Scene scene = optics::Scene::demo();
    int mode = 0;
    QJsonObject workspace;
    std::optional<optics::OptimizationPlan> optimization;
    std::vector<optics::Configuration> configurations;
    std::optional<optics::TolerancePlan> tolerance;
};
QByteArray serializeProject(const Project&);
Project deserializeProject(const QByteArray&, bool validate = true);
void saveProject(const QString&, const Project&);
Project loadProject(const QString&);
Project gullstrandEyeProject();
Project importGeopter(const QByteArray&, const optics::Catalog& catalog = {});
QByteArray exportGeopter(const Project&, bool preserveNative = true);
void reindexOptimization(Project&, size_t at, size_t removed, size_t inserted);

Project foldedMirrorProject();
Project coordinateGroupProject();
Project afocalTelescopeProject();

Project cylindricalLensProject();
Project toroidalLensProject();
Project obscuredApertureProject();
void writeStudies(QJsonObject&,const Project&);
void readStudies(const QJsonObject&,Project&,bool validate);
void reindexStudies(Project&,const std::vector<size_t>&);
Project configurationProject(const Project&,size_t index);
Project realizedProject(const Project&,const optics::ToleranceSample&,size_t configuration,bool compensated=true);
QJsonObject toleranceReportJson(const optics::ToleranceReport&);
Project conjugatesProject();

Project freeformProject();
