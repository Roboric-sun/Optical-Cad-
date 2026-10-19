#pragma once
#include "optics/model.hpp"
namespace optics::detail {
void setVariable(SequentialSystem&, const OptimizationVariable&, double);
OptimizationResult coordinateOptimize(SequentialSystem&, const Catalog&, const OptimizationPlan&,
    std::function<bool(size_t,size_t,double)>);
inline void refocus(SequentialSystem& s, const Catalog& c, const OptimizationPlan& p) {
    if (p.allFieldsFocus) autofocusAllFields(s,c,p.pupilGrid); else autofocus(s,c);
}
}
