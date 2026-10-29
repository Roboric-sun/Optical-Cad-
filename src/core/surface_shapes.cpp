#include "optics/model.hpp"
#include <algorithm>
#include <limits>
namespace optics {
bool hasAnamorphicSurfaces(const SequentialSystem& s){return std::any_of(s.surfaces.begin(),s.surfaces.end(),[](const auto& a){return a.profile!=SurfaceProfile::Standard;});}
bool hasAdvancedApertures(const SequentialSystem& s){return std::any_of(s.surfaces.begin(),s.surfaces.end(),[](const auto& a){return a.aperture.shape!=ApertureShape::Circle || a.aperture.obscuration!=0;});}
bool usesAdvancedSurfaces(const SequentialSystem& s){return hasAnamorphicSurfaces(s)||hasAdvancedApertures(s);}
bool usesPhysicalStop(const SequentialSystem& s){return hasCoordinates(s)||hasAnamorphicSurfaces(s);}
bool insideSurfaceAperture(const Surface& s,double x,double y) {
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(s.semiDiameter)||s.semiDiameter<=0||!std::isfinite(s.aperture.obscuration)||s.aperture.obscuration<0||s.aperture.halfX<=0||s.aperture.halfY<=0)return false;
    const double r=std::hypot(x,y);if(x*x+y*y>s.semiDiameter*s.semiDiameter+1e-9 || (s.aperture.obscuration>0 && r<s.aperture.obscuration))return false;
    switch(s.aperture.shape) {
    case ApertureShape::Circle:return true;
    case ApertureShape::Ellipse:return std::hypot(x/s.aperture.halfX,y/s.aperture.halfY)<=1+1e-12;
    case ApertureShape::Rectangle:return std::abs(x)<=s.aperture.halfX+1e-12 && std::abs(y)<=s.aperture.halfY+1e-12;
    }
    return false;
}
static double conicSag(double r,double k,double h) {
    if(r==0)return 0;
    const double a=1-(1+k)*(h/r)*(h/r);
    return a>=0 ? h*(h/r)/(1+std::sqrt(a)) : std::numeric_limits<double>::quiet_NaN();
}
static bool polynomial(const Surface& s){for(double a:s.asphere)if(a!=0)return true;for(double a:s.oddAsphere)if(a!=0)return true;return false;}
double shapedSag(const Surface& s,double x,double y) {
    if(polynomial(s))return std::numeric_limits<double>::quiet_NaN();
    if(s.profile==SurfaceProfile::CylinderX)return conicSag(s.radius,s.conic,x);
    if(s.profile==SurfaceProfile::CylinderY)return conicSag(s.radius,s.conic,y);
    if(s.profile!=SurfaceProfile::Toroidal || s.conic!=0)return std::numeric_limits<double>::quiet_NaN();
    const double zy=conicSag(s.radius,0,y);
    if(s.rotationRadius==0)return zy;
    const double h=s.rotationRadius-zy;
    if(h==0 || std::signbit(h)!=std::signbit(s.rotationRadius))return std::numeric_limits<double>::quiet_NaN();
    return zy+conicSag(h,0,x);
}
Vec3 shapedNormal(const Surface& s,double x,double y) {
    if(!std::isfinite(shapedSag(s,x,y)))throw std::invalid_argument("Точка вне действительной ветви профиля");
    auto derivative=[](double r,double k,double h){if(r==0)return 0.;const double a=1-(1+k)*(h/r)*(h/r);if(a<=0)throw std::invalid_argument("Сингулярная нормаль профиля");return h/(r*std::sqrt(a));};
    double dx=0,dy=0;
    if(s.profile==SurfaceProfile::CylinderX)dx=derivative(s.radius,s.conic,x);
    else if(s.profile==SurfaceProfile::CylinderY)dy=derivative(s.radius,s.conic,y);
    else {
        const double zy=conicSag(s.radius,0,y),h=s.rotationRadius-zy;
        dy=derivative(s.radius,0,y);
        if(s.rotationRadius!=0){const double a=1-(x/h)*(x/h);if(a<=0)throw std::invalid_argument("Сингулярная нормаль тора");dx=x/(h*std::sqrt(a));dy/=std::sqrt(a);}
    }
    return Vec3{-dx,-dy,1}.unit();
}
// Real polynomial roots: derivative partitions monotonic intervals and retains
// repeated roots at stationary points. Toroidal candidates are verified against sag.
using Polynomial=std::vector<double>;
static double value(const Polynomial& p,double x){double r=0;for(auto i=p.rbegin();i!=p.rend();++i)r=r*x+*i;return r;}
static std::vector<double> roots(Polynomial p) {
    while(p.size()>1 && p.back()==0)p.pop_back();
    if(p.size()==1)return {};
    if(p.size()==2)return {-p[0]/p[1]};
    const double leading=p.back();for(double& a:p)a/=leading;
    double bound=1;for(size_t i=0;i+1<p.size();++i)bound=std::max(bound,1+std::abs(p[i]));
    Polynomial derivative;for(size_t i=1;i<p.size();++i)derivative.push_back(i*p[i]);
    auto critical=roots(derivative);std::vector<double> cuts{-bound};
    for(double x:critical)if(x>-bound && x<bound)cuts.push_back(x);cuts.push_back(bound);std::sort(cuts.begin(),cuts.end());
    std::vector<double> result;
    for(double x:cuts){double scale=0;for(auto i=p.rbegin();i!=p.rend();++i)scale=scale*std::abs(x)+std::abs(*i);if(std::abs(value(p,x))<=1e-12*std::max(1.,scale))result.push_back(x);}
    for(size_t i=1;i<cuts.size();++i){double a=cuts[i-1],b=cuts[i],fa=value(p,a),fb=value(p,b);if((fa<0)==(fb<0) || fa==0 || fb==0)continue;
        for(int n=0;n<90;++n){const double mid=(a+b)/2,f=value(p,mid);if((f<0)==(fa<0)){a=mid;fa=f;}else b=mid;}result.push_back((a+b)/2);}
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),result.end());return result;
}
std::optional<SurfaceHit> intersectToroidal(const Surface& s,const RigidFrame& pose,const Ray& ray,bool aperture) {
    if(s.conic!=0 || polynomial(s))return {};
    if(s.rotationRadius==0 || s.radius==0 || s.rotationRadius==s.radius) {
        auto equivalent=s;
        equivalent.profile=s.rotationRadius==s.radius?SurfaceProfile::Standard:s.rotationRadius==0?SurfaceProfile::CylinderY:SurfaceProfile::CylinderX;
        if(s.radius==0)equivalent.radius=s.rotationRadius;
        equivalent.rotationRadius=0;return intersectFramedSurface(equivalent,pose,ray,aperture);
    }
    const Vec3 origin=pose.local(ray.origin),direction=pose.inverseRotate(ray.direction);
    if(!finite(origin)||!finite(direction)||!std::isfinite(direction.norm2())||direction.norm2()<1e-20)return {};
    const double offset=(Vec3{0,0,s.rotationRadius}-origin).dot(direction)/direction.norm2();
    const Vec3 centered=origin+direction*offset-Vec3{0,0,s.rotationRadius};
    const double scale=std::max({1.,std::abs(s.rotationRadius),std::abs(s.radius),centered.norm()});
    const Vec3 o=centered/scale,d=direction;const double R=s.rotationRadius/scale-s.radius/scale,r=s.radius/scale;
    const double q0=o.norm2()+R*R-r*r,q1=2*o.dot(d),q2=d.norm2();
    Polynomial polynomial{q0*q0-4*R*R*(o.x*o.x+o.z*o.z),2*q0*q1-8*R*R*(o.x*d.x+o.z*d.z),q1*q1+2*q0*q2-4*R*R*(d.x*d.x+d.z*d.z),2*q1*q2,q2*q2};
    if(!std::isfinite(scale)||!std::isfinite(offset)||!std::all_of(polynomial.begin(),polynomial.end(),[](double a){return std::isfinite(a);}))return {};
    for(double u:roots(polynomial)) {
        double t=offset+u*scale;if(t < -1e-7 || !std::isfinite(t))continue;
        const auto initial=origin+direction*t;const double initialSag=shapedSag(s,initial.x,initial.y);
        if(!std::isfinite(initialSag) || std::abs(initial.z-initialSag)>1e-4)continue;
        // Refine only a candidate already on the central sag sheet.
        Vec3 point;
        for(int k=0;k<12;++k){point=origin+direction*t;const double z=shapedSag(s,point.x,point.y);if(!std::isfinite(z))break;
            if(std::abs(point.z-z)<1e-10)break;
            const auto normal=shapedNormal(s,point.x,point.y);const double slope=direction.dot(normal)/normal.z;
            if(std::abs(slope)<1e-14)break;t-=(point.z-z)/slope;
        }
        point=origin+direction*t;const double z=shapedSag(s,point.x,point.y);
        if(t < -1e-7 || !std::isfinite(z) || std::abs(point.z-z)>1e-7 || (aperture && !insideSurfaceAperture(s,point.x,point.y)))continue;
        return SurfaceHit{pose.world(point),pose.rotate(shapedNormal(s,point.x,point.y)),std::max(0.,t)};
    }
    return {};
}
}
