#include "window.hpp"
#include "automation.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <stdexcept>
using namespace optics;
size_t surfaceShapeGUIChecks(Window& w,const QString& dir){
 const auto original=w.project();size_t count=0;
 auto check=[&](bool ok,const char* text){++count;if(!ok)throw std::runtime_error(text);};
 auto near=[&](double a,double b,double eps,const char* text){check(std::isfinite(a)&&std::abs(a-b)<eps,text);};
 auto rejects=[&](auto fn,const char* text){bool bad=false;try{fn();}catch(const std::exception&){bad=true;}check(bad,text);};
 auto torus=toroidalLensProject();auto json=QJsonDocument::fromJson(serializeProject(torus)).object();
 check(json["version"]==10,"New shape and aperture metadata requires format 10");
 check(serializeProject(deserializeProject(serializeProject(torus)))==serializeProject(torus),"Signed toroidal radii and rectangle aperture roundtrip exactly");
 auto old=json;old["version"]=9;rejects([&]{deserializeProject(QJsonDocument(old).toJson(),false);},"Old file version cannot conceal new profile metadata");
 auto bad=json;auto seq=bad["sequential"].toObject();auto rows=seq["surfaces"].toArray();auto row=rows[0].toObject();auto shape=row["shape"].toObject();shape["profile"]=.5;row["shape"]=shape;rows[0]=row;seq["surfaces"]=rows;bad["sequential"]=seq;
 rejects([&]{deserializeProject(QJsonDocument(bad).toJson());},"Fractional profile enum is rejected");
 auto coord=torus;coord.system.surfaces[0].coordinates.mode=CoordinateMode::Relative;coord.system.surfaces[0].coordinates.shift.y=.5;
 check(deserializeProject(serializeProject(coord)).system.surfaces[0].coordinates.shift.y==.5,"Format 10 preserves coordinate transforms from format 9");
 rejects([&]{exportGeopter(torus);},"Geopter export refuses unsupported toroid instead of flattening it");
 Project plain;plain.workspace["sequentialViews"]=QJsonArray{0};w.setProject(plain);const auto baseline=serializeProject(w.project());bool saved=false,invalidStayed=false;
 QTimer::singleShot(0,&w,[&]{auto* dialog=w.findChild<QDialog*>("shapeDialog");if(!dialog)return;
  dialog->findChild<QComboBox*>("surfaceProfile")->setCurrentIndex(3);dialog->findChild<QDoubleSpinBox*>("rotationRadius")->setValue(80);
  dialog->findChild<QDoubleSpinBox*>("mechanicalSemiDiameter")->setValue(6);dialog->findChild<QComboBox*>("surfaceAperture")->setCurrentIndex(1);
  auto* x=dialog->findChild<QDoubleSpinBox*>("apertureHalfX");auto* y=dialog->findChild<QDoubleSpinBox*>("apertureHalfY");x->setValue(10);y->setValue(10);
  dialog->findChild<QAbstractButton*>("shapeSave")->click();invalidStayed=dialog->isVisible()&&!dialog->findChild<QLabel*>("shapeMessage")->text().isEmpty();
  x->setValue(4);y->setValue(2);dialog->findChild<QDoubleSpinBox*>("apertureObscuration")->setValue(.5);
  if(!dir.isEmpty())dialog->grab().save(dir+"/shape_editor.png");
  dialog->findChild<QAbstractButton*>("shapeSave")->click();saved=dialog->result()==QDialog::Accepted;if(!saved)dialog->reject();
 });
 w.findChild<QAction*>("shapeAction")->trigger();check(saved&&invalidStayed,"Real shape editor rejects invalid mask then accepts a valid toroid");
 const auto& s=w.project().system.surfaces[0];check(s.profile==SurfaceProfile::Toroidal&&s.rotationRadius==80&&s.aperture.shape==ApertureShape::Ellipse,"GUI stores chosen profile and aperture");near(s.aperture.obscuration,.5,1e-12,"GUI stores central obscuration radius");
 w.findChild<QAction*>("undoAction")->trigger();check(serializeProject(w.project())==baseline,"Undo restores full project after shape edit");
 w.findChild<QAction*>("redoAction")->trigger();check(w.project().system.surfaces[0].profile==SurfaceProfile::Toroidal,"Redo restores new surface profile");
 QTemporaryDir temp;
 for(const auto& project:{cylindricalLensProject(),torus,obscuredApertureProject()}){
  check(project.system.validate(project.catalog).empty(),"New demonstration validates with actual material catalog");
  saveProject(temp.path()+"/shape.optcad",project);check(serializeProject(loadProject(temp.path()+"/shape.optcad"))==serializeProject(project),"New shape project actually saves and reopens from disk");
  w.setProject(project);w.recalculate();check(w.results()->error.isEmpty()&&!w.results()->spots[0].samples.empty(),"New profiles and obscuration have working geometric spots");
  check(!w.results()->waveError.isEmpty()&&!w.results()->diffractionError.isEmpty(),"Unvalidated new wave/diffraction models have explicit diagnostics");
  PlotWidget plot(View::Layout);plot.resize(1050,600);plot.setData(w.results());plot.layoutProjection=2;plot.show();QTest::qWait(30);check(!plot.axisViews().empty(),"Aperture XY layout produces real axes");
  if(!dir.isEmpty())plot.grab().save(dir+QString("/shape_%1.png").arg(int(project.system.surfaces[0].profile)));
  const auto request=QJsonDocument::fromJson(serializeProject(project)).object();const auto api=executeRequest({{"operation","analyze"},{"project",request},{"options",QJsonObject{{"pupil_grid",9}}}});
  near(api["fields"].toArray()[0].toObject()["rms_mm"].toDouble(),spot(project.system,project.catalog,project.system.fields[0],9).rms,1e-10,"Batch geometry agrees with core for new shapes");
  const auto first=executeRequest({{"operation","first_order"},{"project",request}});check(first["matrix"].toArray().size()==4,"New shape has actual first-order matrix through shared core");
 }
 w.setProject(original);return count;
}
