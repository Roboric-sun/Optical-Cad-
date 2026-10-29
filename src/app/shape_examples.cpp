#include "project.hpp"
#include <QJsonArray>
using namespace optics;
Project cylindricalLensProject(){
 Project p;p.system.name="Цилиндрическая линза · эллиптическая апертура";p.system.pupilDiameter=3;p.system.fields={{0,0,1}};
 for(auto& s:p.system.surfaces){s.profile=SurfaceProfile::CylinderX;s.semiDiameter=5;}
 p.system.surfaces[0].aperture={ApertureShape::Ellipse,1.5,1,0};
 p.workspace["sequentialViews"]=QJsonArray{0,1};return p;
}
Project toroidalLensProject(){
 Project p;p.system.name="Тороидальная линза · разные фокусы X/Y";p.system.pupilDiameter=6;p.system.fields={{0,0,1}};
 for(size_t i=0;i<2;++i){auto& s=p.system.surfaces[i];s.profile=SurfaceProfile::Toroidal;s.rotationRadius=i?-80:80;s.semiDiameter=6;s.aperture={ApertureShape::Rectangle,4,3,0};}
 p.system.surfaces.back().thickness=60;p.workspace["sequentialViews"]=QJsonArray{0,1};return p;
}
Project obscuredApertureProject(){
 Project p;p.system.name="Кольцевая апертура · центральное экранирование";p.system.pupilDiameter=4;p.system.fields={{0,0,1}};
 p.system.surfaces[0].semiDiameter=5;p.system.surfaces[0].aperture.obscuration=.7;
 p.workspace["sequentialViews"]=QJsonArray{0,1};return p;
}
