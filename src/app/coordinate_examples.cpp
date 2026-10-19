#include "project.hpp"
#include <QJsonArray>
using namespace optics;
Project foldedMirrorProject() {
    Project p;p.system.name="Сложенный ход — зеркало 45°";
    Surface mirror;mirror.name="Плоское зеркало";mirror.kind=SurfaceKind::Mirror;mirror.tilt.x=45;mirror.thickness=0;mirror.semiDiameter=5;
    Surface screen;screen.name="Плоскость после поворота";screen.kind=SurfaceKind::Stop;screen.thickness=20;screen.semiDiameter=5;
    screen.coordinates.mode=CoordinateMode::Relative;screen.coordinates.shift={0,20,0};screen.coordinates.tilt={-90,0,0};
    p.system.surfaces={mirror,screen};p.system.stop=0;p.system.fields={{0,0,1},{0,.5,1}};
    p.system.pupilDiameter=3;p.workspace["sequentialViews"]=QJsonArray{0,1};return p;
}
Project coordinateGroupProject() {
    Project p;p.system.name="Смещённая линза — возврат к глобальной системе";
    auto& front=p.system.surfaces[0];front.coordinates.mode=CoordinateMode::Relative;
    front.coordinates.shift={0,2,0};front.coordinates.tilt={3,0,0};
    p.system.surfaces[1].thickness=0;
    Surface exit;exit.name="Возврат к глобальной плоскости";exit.kind=SurfaceKind::Stop;
    exit.thickness=3;exit.coordinates.mode=CoordinateMode::ReturnFrame;exit.coordinates.shift={0,0,49};
    p.system.surfaces.push_back(exit);p.system.pupilDiameter=3;p.system.fields={{0,0,1},{0,.5,1}};
    p.workspace["sequentialViews"]=QJsonArray{0,1};return p;
}
Project afocalTelescopeProject() {
    Project p;p.system.name="Афокальный телескоп — увеличение −2";
    const double n=p.catalog.get("N-BK7").index(.5875618);
    Surface a,b,c,d;
    const double thickness=5,phi=(n-1)/100;
    const double power1=-2*phi+thickness*phi*phi/n;
    const double power2=2*power1;
    const double phi2=-power2/(1+std::sqrt(1+thickness/n*power2));
    a.radius=100;b.radius=-100;c.radius=(n-1)/phi2;d.radius=-c.radius;
    const double A1=1-thickness*phi/n,D2=1-thickness*phi2/n;
    a.thickness=c.thickness=thickness;b.thickness=-A1/power1-D2/power2;d.thickness=10;
    a.material=c.material="N-BK7";a.name="Объектив, передняя";b.name="Объектив, задняя";
    c.name="Окуляр, передняя";d.name="Окуляр, задняя";
    p.system.surfaces={a,b,c,d};p.system.pupilDiameter=2;p.system.fields={{0,0,1},{0,.2,1}};
    p.system.wavelengths={{.5875618,1}};p.system.primary=0;p.workspace["sequentialViews"]=QJsonArray{0};return p;
}
