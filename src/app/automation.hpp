#pragma once
#include "project.hpp"
// One operation boundary shared by the batch executable and Python client.
// Every mutating operation works on a local project and returns a new snapshot.
QJsonObject executeRequest(const QJsonObject& request);
