#include "optics/model.hpp"
#include <algorithm>
namespace optics {
bool hasCoordinates(const SequentialSystem& s) {
    return std::any_of(s.surfaces.begin(),s.surfaces.end(),[](const auto& a){return a.coordinates.mode!=CoordinateMode::None;});
}
SequentialFrames sequentialFrames(const SequentialSystem& s,const Catalog& c) {
    return s.solves.empty()?sequentialFrames(s):sequentialFrames(resolvedSystem(s,c));
}
SequentialFrames sequentialFrames(const SequentialSystem& s) {
    if(!s.solves.empty())return sequentialFrames(resolvedSystem(s));
    SequentialFrames out;RigidFrame axis;
    for(size_t i=0;i<s.surfaces.size();++i) {
        const auto& a=s.surfaces[i];const auto& change=a.coordinates;
        if(int(change.mode)<0 || int(change.mode)>3 || int(change.order)<0 || int(change.order)>1 ||
           !finite(change.shift) || !finite(change.tilt))throw std::invalid_argument("Некорректное преобразование координат");
        if(change.mode==CoordinateMode::None && (change.shift.norm2()!=0 || change.tilt.norm2()!=0 || change.reference!=-1 || change.order!=CoordinateOrder::TranslateThenTilt))
            throw std::invalid_argument("Отключённое преобразование содержит активные параметры");
        if(change.mode==CoordinateMode::Relative && change.reference!=-1)throw std::invalid_argument("Относительное преобразование не использует ссылку возврата");
        if(change.mode!=CoordinateMode::None) {
            if(change.mode==CoordinateMode::ReturnFrame || change.mode==CoordinateMode::ReturnOrientation) {
                if(change.reference < -1 || change.reference>=int(i))throw std::invalid_argument("Возврат требует глобальную или предыдущую систему координат");
                const auto target=change.reference<0 ? RigidFrame{} : out.nominal.at(size_t(change.reference));
                if(change.mode==CoordinateMode::ReturnFrame)axis=target;else axis.basis=target.basis;
            }
            auto local=RigidFrame::fromPose({change.shift,change.tilt});
            if(change.order==CoordinateOrder::TiltThenTranslate)local.position=local.rotate(change.shift);
            axis=axis.compose(local);
        }
        out.nominal.push_back(axis);
        out.surfaces.push_back(axis.compose(RigidFrame::fromPose({a.decenter,a.tilt})));
        axis.position=axis.world({0,0,a.thickness});
        if(!finite(axis.position))throw std::invalid_argument("Переполнение координат поверхности");
    }
    axis.position=axis.world({0,0,s.defocus});if(!finite(axis.position))throw std::invalid_argument("Переполнение положения изображения");out.image=axis;
    return out;
}
DifferentialFirstOrder differentialFirstOrder(const SequentialSystem& source,const Catalog& c,double wavelength,double step) {
    const auto s=resolvedSystem(source,c);const auto errors=s.validate(c);
    if(!errors.empty())throw std::invalid_argument(errors.front());
    if(!std::isfinite(step) || step<1e-7 || step>1e-2)throw std::invalid_argument("Шаг первого порядка: 1e-7…1e-2");
    const auto frames=sequentialFrames(s);const auto image=frames.image;
    const auto& first=frames.surfaces.front();const double radius=clearSemiDiameters(s).front();
    const double margin=radius*(std::abs(first.basis[0].z)+std::abs(first.basis[1].z))+std::abs(sag(s.surfaces.front(),radius,0)*first.basis[2].z);
    const double launchZ=std::min(-30.,first.position.z-margin-1);
    auto evaluate=[&](std::array<double,4> x) {
        Ray ray{{x[0]+launchZ*x[2],x[1]+launchZ*x[3],launchZ},{x[2],x[3],1},wavelength};
        const auto t=trace(s,c,ray,true,SIZE_MAX,false);
        if(t.status!=TraceStatus::Complete)throw std::invalid_argument("Опорный луч первого порядка не достигает изображения");
        const auto direction=image.inverseRotate(t.exitDirection);
        if(std::abs(direction.z)<1e-8)throw std::invalid_argument("Луч параллелен локальному изображению");
        const auto p=image.local(t.image);
        return std::pair{t,std::array<double,4>{p.x,p.y,t.index*direction.x/direction.z,t.index*direction.y/direction.z}};
    };
    DifferentialFirstOrder out;const auto center=evaluate({});out.chiefImage=center.first.imageLocal;
    out.chiefDirection=image.inverseRotate(center.first.exitDirection);out.outputIndex=center.first.index;
    for(size_t j=0;j<4;++j) {
        std::array<double,4> plus{},minus{};plus[j]=step;minus[j]=-step;
        const auto a=evaluate(plus).second,b=evaluate(minus).second;
        for(size_t i=0;i<4;++i){out.matrix[4*i+j]=(a[i]-b[i])/(2*step);if(!std::isfinite(out.matrix[4*i+j]))throw std::invalid_argument("Переполнение дифференциального первого порядка");}
    }
    double power=0;for(size_t r:{size_t(2),size_t(3)})for(size_t j:{size_t(0),size_t(1)})power=std::hypot(power,out.matrix[4*r+j]);
    out.afocal=power<1e-8;return out;
}
}
