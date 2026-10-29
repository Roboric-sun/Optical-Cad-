#pragma once
#include "model.hpp"

namespace optics {
// Indices refer to surfaces, except FieldX/Y/Weight (fields); system parameters ignore index.
enum class DesignParameter { Radius, Thickness, Conic, A4, A6, A8, A10, Defocus,
    DecenterX, DecenterY, DecenterZ, TiltX, TiltY, TiltZ, IndexOffset, RotationRadius,
    ObjectDistance, PupilDiameter, FieldX, FieldY, FieldWeight, Curvature, XY20, XY11, XY02, XY30, XY21, XY12, XY03 };
struct DesignTarget {
    DesignParameter parameter=DesignParameter::Radius;
    size_t index=0;
    bool operator==(const DesignTarget&) const = default;
};
bool surfaceTarget(DesignTarget);
bool fieldTarget(DesignTarget);
const char* designParameterName(DesignParameter);
double designValue(const SequentialSystem&,const Catalog&,DesignTarget);
void setDesignValue(SequentialSystem&,Catalog&,DesignTarget,double);
struct ConfigurationValue {
    DesignTarget target;
    double value=0;
    bool variable=false;
    double lower=-1,upper=1,step=.1;
};
struct ConfigurationMaterial { size_t surface=0; std::string material="AIR"; };
struct Configuration {
    std::string name="Состояние";
    double weight=1;
    std::vector<ConfigurationValue> values;
    std::vector<ConfigurationMaterial> materials;
};
struct OpticalDesign { SequentialSystem system; Catalog catalog; };
std::vector<std::string> validateConfigurations(const OpticalDesign&,const std::vector<Configuration>&);
OpticalDesign configurationDesign(const OpticalDesign&,const Configuration&);
struct ConfigurationEvaluation { double score=0; std::vector<double> scores; };
ConfigurationEvaluation evaluateConfigurations(const OpticalDesign&,const std::vector<Configuration>&,const OptimizationPlan&);
// Shared variables come from the existing merit editor; local variables are flagged overrides.
// A bounded coordinate search commits both the base and states only on successful completion.
OptimizationResult optimizeConfigurations(OpticalDesign&,std::vector<Configuration>&,const OptimizationPlan&,
    std::function<bool(size_t,size_t,double)> progress={});
enum class ToleranceDistribution { Uniform, Normal };
struct ToleranceOperand {
    DesignTarget target;
    double amplitude=.01,mean=0; // Uniform: half width; Normal: 3 sigma, untruncated.
    ToleranceDistribution distribution=ToleranceDistribution::Uniform;
};
struct Compensator { DesignTarget target{DesignParameter::Defocus,0}; double lower=-1,upper=1,step=.1; };
struct TolerancePlan {
    std::vector<ToleranceOperand> operands;
    std::vector<Compensator> compensators;
    std::vector<std::vector<double>> correlation; // Gaussian copula; empty = identity.
    size_t samples=64,compensationIterations=12;
    uint64_t seed=42;
    double limit=3;
    std::vector<std::string> validate(const OpticalDesign&,const std::vector<Configuration>&) const;
};
struct YieldEstimate { size_t accepted=0,total=0; double fraction=0,lower=0,upper=1; };
YieldEstimate wilsonYield(size_t accepted,size_t total);
std::vector<std::vector<double>> toleranceDraws(const TolerancePlan&,size_t count);
struct ToleranceSample {
    std::vector<double> deltas,corrections;
    std::optional<double> before,after;
    std::string beforeError,afterError;
};
struct ToleranceSensitivity { size_t operand=0; ToleranceSample minus,plus; double impact=0; };
struct ToleranceReport {
    double nominal=0;
    bool cancelled=false;
    std::vector<ToleranceSensitivity> sensitivities;
    std::vector<ToleranceSample> samples;
    YieldEstimate before,after;
    std::vector<YieldEstimate> convergence;
};
ToleranceReport runTolerances(const OpticalDesign&,const std::vector<Configuration>&,const OptimizationPlan&,
    const TolerancePlan&,std::function<bool(size_t,size_t,double)> progress={});
// Manufacturing perturbations act on resolved geometry; nominal design solves are not re-applied.
OpticalDesign toleranceRealization(const OpticalDesign&,const Configuration&,const TolerancePlan&,
    const ToleranceSample&,bool compensated=true);
} // namespace optics
