#pragma once
#include "window.hpp"
#include <QTest>

inline std::shared_ptr<Results> readyResults(Window& w) {
    if (w.analyzing() && !QTest::qWaitFor([&] { return !w.analyzing(); }, 60000))
        throw std::runtime_error("Timed out waiting for the current sequential analysis");
    return w.results();
}
