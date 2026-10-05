#pragma once
#include "optics/model.hpp"
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
};
QByteArray serializeProject(const Project&);
Project deserializeProject(const QByteArray&, bool validate = true);
void saveProject(const QString&, const Project&);
Project loadProject(const QString&);
Project importGeopter(const QByteArray&, const optics::Catalog& catalog = {});
QByteArray exportGeopter(const Project&, bool preserveNative = true);
void reindexOptimization(Project&, size_t at, size_t removed, size_t inserted);
