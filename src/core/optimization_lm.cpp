#include "optimization_internal.hpp"
#include <algorithm>
#include <limits>
#include <numeric>


namespace optics {
namespace {
struct Cancelled {};
using Vector=std::vector<double>;
using Matrix=std::vector<Vector>;
// Householder QR of [J; sqrt(lambda) D], without forming J^T J.
Vector dampedStep(const Matrix& jacobian,const Vector& residual,double damping) {
    const size_t n=jacobian.front().size(), m=jacobian.size();
    Matrix a(m+n,Vector(n)); Vector b(m+n);
    for(size_t i=0;i<m;++i){a[i]=jacobian[i];b[i]=-residual[i];}
    for(size_t j=0;j<n;++j) {
        double norm=0;for(size_t i=0;i<m;++i)norm=std::hypot(norm,a[i][j]);
        a[m+j][j]=std::sqrt(damping)*std::max(norm,1e-8);
    }
    for(size_t j=0;j<n;++j) {
        double norm=0;for(size_t i=j;i<a.size();++i)norm=std::hypot(norm,a[i][j]);
        if(!(norm>0) || !std::isfinite(norm))throw std::runtime_error("Вырожденный шаг DLS");
        const double alpha=-std::copysign(norm,a[j][j]);
        Vector v(a.size()-j);for(size_t i=j;i<a.size();++i)v[i-j]=a[i][j];
        v[0]-=alpha;
        double vnorm=0;for(double x:v)vnorm=std::hypot(vnorm,x);
        for(double& x:v)x/=vnorm;
        for(size_t column=j;column<n;++column) {
            double dot=0;for(size_t i=j;i<a.size();++i)dot+=v[i-j]*a[i][column];
            for(size_t i=j;i<a.size();++i)a[i][column]-=2*v[i-j]*dot;
        }
        double dot=0;for(size_t i=j;i<a.size();++i)dot+=v[i-j]*b[i];
        for(size_t i=j;i<a.size();++i)b[i]-=2*v[i-j]*dot;
    }
    Vector step(n);
    for(size_t k=n;k-->0;) {
        double value=b[k];for(size_t j=k+1;j<n;++j)value-=a[k][j]*step[j];
        step[k]=value/a[k][k];
        if(!std::isfinite(step[k]))throw std::runtime_error("Неконечный шаг DLS");
    }
    return step;
}
OptimizationResult lmOptimize(SequentialSystem& system,const Catalog& c,const OptimizationPlan& p,
    const std::function<bool(size_t,size_t,double)>& progress) {
    OptimizationResult out;
    const size_t n=p.variables.size(), maximum=1+(8*n+12)*p.iterations;
    auto poll=[&]{if(progress && !progress(out.evaluations,maximum,out.after))throw Cancelled{};};
    auto current=system;
    auto evaluate=[&](SequentialSystem& trial) {
        poll(); ++out.evaluations;
        applySolves(trial,c);
        if(p.refocus)detail::refocus(trial,c,p);
        return evaluateMerit(trial,c,p);
    };
    try {
        auto merit=evaluate(current);
        out.before=out.after=merit.score;out.history.push_back(out.after);
        double lambda=1e-3;
        out.termination="Лимит итераций";
        for(size_t iteration=0;iteration<p.iterations;++iteration) {
            poll();
            Matrix jacobian(merit.residuals.size(),Vector(n));
            bool unavailable=false;
            for(size_t j=0;j<n;++j) {
                const auto v=p.variables[j]; const double x=variableValue(current,v);
                std::optional<MeritEvaluation> plus,minus; double hp=0,hm=0;
                for(int retry=0;retry<4 && !plus && !minus;++retry) {
                    const double h=v.step*1e-4*std::pow(.1,retry);
                    const double hi=std::min(v.upper,std::max(x+h,std::nextafter(x,v.upper)));
                    const double lo=std::max(v.lower,std::min(x-h,std::nextafter(x,v.lower)));
                    if(hi>x) {
                        auto trial=current;detail::setVariable(trial,v,hi);hp=(hi-x)/v.step;
                        try{plus=evaluate(trial);}catch(const Cancelled&){throw;}catch(const std::exception&){}
                    }
                    if(lo<x) {
                        auto trial=current;detail::setVariable(trial,v,lo);hm=(x-lo)/v.step;
                        try{minus=evaluate(trial);}catch(const Cancelled&){throw;}catch(const std::exception&){}
                    }
                }
                if(!plus && !minus){unavailable=true;break;}
                for(size_t r=0;r<merit.residuals.size();++r) {
                    // Nonuniform central differences remain second order near a bound.
                    if(plus && minus)
                        jacobian[r][j]=(hm/(hp+hm))*(plus->residuals[r]-merit.residuals[r])/hp+
                                       (hp/(hp+hm))*(merit.residuals[r]-minus->residuals[r])/hm;
                    else if(plus)jacobian[r][j]=(plus->residuals[r]-merit.residuals[r])/hp;
                    else jacobian[r][j]=(merit.residuals[r]-minus->residuals[r])/hm;
                }
            }
            if(unavailable){out.termination="Производная недоступна у границы допустимой геометрии";break;}
            double gradient=0;std::vector<bool> active(n,false);
            for(size_t j=0;j<n;++j) {
                double g=0,norm=0;for(size_t r=0;r<jacobian.size();++r){g+=jacobian[r][j]*merit.residuals[r];norm=std::hypot(norm,jacobian[r][j]);}
                const auto v=p.variables[j];const double x=variableValue(current,v);
                if(norm==0 || (x<=v.lower && g>0) || (x>=v.upper && g<0)) {
                    active[j]=true;for(auto& row:jacobian)row[j]=0;
                } else gradient=std::max(gradient,std::abs(g)/std::max(norm,1e-12));
            }
            if(gradient<1e-9*std::max(1.,merit.score)){out.termination="Малый проецированный градиент";break;}
            bool accepted=false,small=false;
            for(int attempt=0;attempt<12;++attempt) {
                poll();
                const auto direction=dampedStep(jacobian,merit.residuals,lambda);
                auto trial=current;Vector step(n);double length=0;
                for(size_t j=0;j<n;++j) {
                    const auto v=p.variables[j];const double x=variableValue(current,v);
                    const double next=active[j] ? x : std::clamp(x+direction[j]*v.step,v.lower,v.upper);
                    detail::setVariable(trial,v,next);step[j]=(next-x)/v.step;
                    length=std::max(length,std::abs(step[j]));
                }
                if(length<1e-9){small=true;break;}
                double predicted=0;
                for(size_t r=0;r<jacobian.size();++r) {
                    double linear=merit.residuals[r];for(size_t j=0;j<n;++j)linear+=jacobian[r][j]*step[j];
                    predicted+=merit.residuals[r]*merit.residuals[r]-linear*linear;
                }
                try {
                    auto candidate=evaluate(trial);
                    const double actual=(merit.score-candidate.score)*(merit.score+candidate.score);
                    if(actual>0 && predicted>0 && actual/predicted>1e-4) {
                        current=std::move(trial);merit=std::move(candidate);out.after=merit.score;
                        lambda=std::max(1e-12,lambda/3);accepted=true;break;
                    }
                }catch(const Cancelled&){throw;}catch(const std::exception&){}
                lambda=std::min(1e16,lambda*10);
            }
            out.history.push_back(out.after);
            if(!accepted){out.termination=small?"Малый шаг":"Допустимый улучшающий шаг не найден";break;}
        }
        poll(); system=std::move(current);
    } catch(const Cancelled&) {out.cancelled=true;out.termination="Отменено";}
    return out;
}
double radicalInverse(size_t index,size_t base) {
    double result=0,factor=1.;while(index){factor/=double(base);result+=factor*(index%base);index/=base;}return result;
}
} // namespace
OptimizationResult optimize(SequentialSystem& system,const Catalog& c,const OptimizationPlan& p,
    std::function<bool(size_t,size_t,double)> progress) {
    auto errors=p.validate(system);if(errors.empty())errors=system.validate(c);
    if(!errors.empty())throw std::invalid_argument(errors.front());
    for(const auto& v:p.variables) {
        const double value=variableValue(system,v);
        if(!std::isfinite(value) || value<v.lower || value>v.upper)
            throw std::invalid_argument("Начальное значение переменной выходит за границы");
    }
    OptimizationResult result; auto best=system;
    const size_t maximum=p.starts*(1+(8*p.variables.size()+12)*p.iterations);
    for(size_t start=0;start<p.starts;++start) {
        if(progress && !progress(result.evaluations,maximum,result.after)){result.cancelled=true;return result;}
        auto trial=system;
        if(start)for(size_t j=0;j<p.variables.size();++j) {
            const auto v=p.variables[j];
            // Repeatable local restarts, no hidden global random state.
            const double offset=(radicalInverse(start+1,2+j*2)-.5)*.5*(v.upper-v.lower);
            detail::setVariable(trial,v,std::clamp(variableValue(system,v)+offset,v.lower,v.upper));
        }
        const size_t offset=result.evaluations;size_t attempted=0;
        const auto callback=[&](size_t done,size_t,double score) {
            attempted=done;
            return !progress || progress(offset+done,maximum,start?result.after:score);
        };
        OptimizationResult local;
        try {
            local=p.algorithm==OptimizationAlgorithm::LevenbergMarquardt ? lmOptimize(trial,c,p,callback) :
                detail::coordinateOptimize(trial,c,p,callback);
        } catch(const std::exception&) {
            if(!start)throw;
            ++attempted; result.evaluations+=attempted;result.starts.push_back({0,0,attempted,false});continue;
        }
        result.evaluations+=local.evaluations;
        if(local.cancelled){result.cancelled=true;result.termination="Отменено";return result;}
        if(!start){result.before=local.before;result.after=local.after;result.history=local.history;best=trial;result.termination=local.termination;}
        else {
            for(double score:local.history)result.history.push_back(std::min(result.after,score));
            if(local.after<result.after){result.after=local.after;best=trial;result.termination=local.termination;}
        }
        result.starts.push_back({local.before,local.after,local.evaluations,true});
    }
    if(progress && !progress(result.evaluations,maximum,result.after)){result.cancelled=true;return result;}
    system=std::move(best);return result;
}
} // namespace optics
