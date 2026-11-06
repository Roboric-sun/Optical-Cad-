#include "optics/model.hpp"
#include <algorithm>
#include <stdexcept>
using namespace optics;
size_t surfaceShapeChecks(){
 size_t count=0;auto check=[&](bool ok,const char* text){++count;if(!ok)throw std::runtime_error(text);};
 auto near=[&](double a,double b,double t,const char* text){check(std::isfinite(a)&&std::abs(a-b)<t,text);};
 Catalog c;Surface s;s.profile=SurfaceProfile::CylinderX;s.radius=50;s.semiDiameter=10;
 const double expected=50-std::sqrt(2500-9);
 near(sag(s,3,4),expected,1e-12,"Cylinder X sag is independent of Y");
 near(sag(s,3,-7),expected,1e-12,"Cylinder X has zero curvature along cylinder axis");
 auto hit=intersectSurface(s,{},{{3,4,-10},{0,0,1},.55});check(bool(hit),"Analytic cylinder intersection exists");
 near(hit->point.z,expected,1e-10,"Cylinder intersection matches independent circle");
 near(-hit->normal.x/hit->normal.z,3/std::sqrt(2500-9),1e-12,"Cylinder normal matches circle derivative");
 near(hit->normal.y,0,1e-12,"Cylinder normal has no component along flat axis");
 s.profile=SurfaceProfile::CylinderY;near(sag(s,4,3),expected,1e-12,"Cylinder Y rotates the powered meridian");
 s.profile=SurfaceProfile::Toroidal;s.radius=50;s.rotationRadius=100;
 for(double ry:{-50.,50.})for(double rx:{-100.,100.,50.}) {
  s.radius=ry;s.rotationRadius=rx;
  for(auto p:{Vec3{2,3,0},Vec3{-3,1,0},Vec3{0,0,0}}) {
   const double z=sag(s,p.x,p.y),R=rx-ry;
   const double q=p.x*p.x+p.y*p.y+(z-rx)*(z-rx)+R*R-ry*ry;
   near(q*q-4*R*R*(p.x*p.x+(z-rx)*(z-rx)),0,1e-5,"Toroidal sag satisfies independent implicit torus equation");
   const auto n=surfaceNormal(s,p.x,p.y);const double e=1e-4;
   near(-n.x/n.z,(sag(s,p.x+e,p.y)-sag(s,p.x-e,p.y))/(2*e),1e-8,"Toroidal X normal agrees with independent finite difference");
   near(-n.y/n.z,(sag(s,p.x,p.y+e)-sag(s,p.x,p.y-e))/(2*e),1e-8,"Toroidal Y normal agrees with independent finite difference");
   auto t=intersectSurface(s,{},{{p.x,p.y,-20},{0,0,1},.55});check(bool(t),"Quartic toroid ray reaches selected central sheet");
   near(t->point.z,z,1e-7,"Toroid root belongs to the central sag sheet for signed radii");
   const Vec3 target{p.x,p.y,z},direction=Vec3{.1,.2,1}.unit();
   auto oblique=intersectSurface(s,{}, {target-direction*20,direction,.55});check(bool(oblique),"Oblique toroidal ray has the constructed known intersection");
   near((oblique->point-target).norm(),0,1e-7,"Oblique quartic intersection recovers independently constructed target");
  }
 }
 s.radius=50;s.rotationRadius=50;near(sag(s,2,3),50-std::sqrt(2500-13),1e-12,"Equal toroidal radii reduce to a sphere");
 s.rotationRadius=0;near(sag(s,2,3),50-std::sqrt(2500-9),1e-12,"Flat rotation radius reduces to Y cylinder");
 s.radius=0;s.rotationRadius=100;near(sag(s,2,3),100-std::sqrt(10000-4),1e-12,"Flat generating curve reduces to X cylinder");
 s.radius=50;s.rotationRadius=100;s.semiDiameter=20;
 auto tangent=intersectSurface(s,{},{{-10,0,0},{1,0,0},.55});check(bool(tangent),"Repeated quartic root at tangency is retained");near(tangent->point.x,0,1e-6,"Tangential root is at toroidal vertex");
 auto first=intersectSurface(s,{},{{-20,0,1},{1,0,0},.55});check(bool(first),"Horizontal toroidal ray has central-sheet intersections");
 near(first->point.x,-std::sqrt(199.),1e-6,"Closest forward central-sheet root is selected");
 s.semiDiameter=25;s.aperture.obscuration=2;
 SequentialSystem screened;screened.surfaces={s};screened.fields={{0,0,1}};screened.wavelengths={{.55,1}};screened.primary=0;
 const Ray crossing{{-5,0,-.4},Vec3{1,0,.1}.unit(),.55};
 auto geometric=intersectSurface(s,{},crossing,false),laterOpen=intersectSurface(s,{},crossing,true);
 check(geometric && laterOpen && std::abs(geometric->point.x)<2 && laterOpen->point.x>2,"Masked toroid has a blocked first root and later open root in the constructed regression");
 check(trace(screened,c,crossing).status==TraceStatus::Vignetted,"Sequential ray cannot tunnel past an opaque first contact to a later root");
 s.aperture={ApertureShape::Ellipse,4,2,0};s.semiDiameter=10;
 check(insideSurfaceAperture(s,4,0)&&!insideSurfaceAperture(s,3,1.8),"Ellipse clips by its own axes rather than enclosing circle");
 s.aperture={ApertureShape::Rectangle,3,2,.5};check(insideSurfaceAperture(s,3,2)&&!insideSurfaceAperture(s,3.1,0),"Rectangle retains corners and excludes points beyond its half width");
 check(!insideSurfaceAperture(s,0,0)&&insideSurfaceAperture(s,.5,0),"Circular obscuration blocks center and includes its boundary");
 auto blocked=intersectSurface(s,{},{{0,0,-10},{0,0,1},.55});check(!blocked,"Obscuration rejects a real central surface hit");
 check(bool(intersectSurface(s,{},{{0,0,-10},{0,0,1},.55},false)),"Aperture-free diagnostic still intersects obscured surface");
 SequentialSystem model;model.surfaces={s};model.fields={{0,0,1}};model.wavelengths={{.55,1}};model.primary=0;model.pupilDiameter=2;
 auto t=trace(model,c,{{0,0,-10},{0,0,1},.55});check(t.status==TraceStatus::Vignetted,"Masked hit reports Vignetted instead of Miss or false refraction");
 Material glass;glass.name="SHAPE_CONST";glass.nd=1.5;c.add(glass);
 s.aperture={};s.profile=SurfaceProfile::CylinderX;s.rotationRadius=0;s.radius=50;s.material=glass.name;s.thickness=10;
 model.surfaces={s};const auto matrix=differentialFirstOrder(model,c,.55);
 near(matrix.matrix[8],-.01,1e-7,"Cylinder refracting X power matches independent paraxial boundary formula");
 near(matrix.matrix[13],0,1e-9,"Cylinder has no Y paraxial power");
 s.profile=SurfaceProfile::Toroidal;s.rotationRadius=100;model.surfaces={s};const auto torus=differentialFirstOrder(model,c,.55);
 near(torus.matrix[8],-.005,1e-7,"Toroidal X power uses rotation radius");near(torus.matrix[13],-.01,1e-7,"Toroidal Y power uses generating radius");
 model.surfaces[0].conic=1;check(!model.validate(c).empty(),"Unsupported toroidal conic is rejected rather than ignored");
 model.surfaces[0]=s;model.surfaces[0].asphere[0]=1e-6;check(!model.validate(c).empty(),"Unsupported new-profile polynomial is rejected");
 model.surfaces[0]=s;model.surfaces[0].aperture={ApertureShape::Rectangle,9,9,0};check(!model.validate(c).empty(),"Aperture corners must fit mechanical circle");
 auto closed=SequentialSystem::demo();closed.surfaces[0].semiDiameter=60;closed.surfaces[0].aperture.obscuration=55;
 check(!closed.validate(c).empty(),"Obscuration cannot close all of the actual spherical domain");
 closed.surfaces[0].aperture={ApertureShape::Ellipse,55,2,0};check(!closed.validate(c).empty(),"Mask cannot extend outside actual conic sag domain");
 // Finite grid sample count independently checks ray survival for an annular mask.
 auto annular=SequentialSystem::demo();annular.fields={{0,0,1}};annular.wavelengths={{.55,1}};annular.primary=0;annular.pupilDiameter=4;
 annular.surfaces[0].aperture.obscuration=.7;const auto spotData=spot(annular,c,{},9);
 size_t launched=0,survived=0;for(int y=-4;y<=4;++y)for(int x=-4;x<=4;++x){if(x*x+y*y>16)continue;++launched;if(.5*std::hypot(x,y)>=.7)++survived;}
 check(spotData.launched==launched && spotData.samples.size()==survived,"Annular geometric survival matches independent input-grid count");
 check(!defaultOptimization(annular,c).operands.empty()&&defaultOptimization(annular,c).validate(annular).empty(),"Default geometric optimization remains usable with masked pupil");
 return count;
}
