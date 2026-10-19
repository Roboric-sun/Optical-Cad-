#include "window.hpp"
#include "coordinate_editor.hpp"
#include "automation.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <stdexcept>
using namespace optics;
size_t coordinateGUIChecks(Window& w,const QString& dir) {
    const auto original=w.project();size_t count=0;
    auto check=[&](bool ok,const char* msg){++count;if(!ok)throw std::runtime_error(msg);};
    auto near=[&](double a,double b,double eps,const char* msg){check(std::isfinite(a)&&std::abs(a-b)<eps,msg);};
    auto rejects=[&](auto fn,const char* msg){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,msg);};
    auto folded=foldedMirrorProject();auto bytes=serializeProject(folded);auto json=QJsonDocument::fromJson(bytes).object();
    check(json["version"]==9,"Coordinate system requires format 9");
    check(serializeProject(deserializeProject(bytes))==bytes,"Coordinates, order and return metadata roundtrip exactly");
    auto old=json;old["version"]=8;rejects([&]{deserializeProject(QJsonDocument(old).toJson(),false);},"Older formats cannot conceal coordinate transforms");
    auto bad=json;auto seq=bad["sequential"].toObject();auto surfaces=seq["surfaces"].toArray();auto row=surfaces[1].toObject();auto change=row["coordinates"].toObject();change["mode"]=3;change["reference"]=1;row["coordinates"]=change;surfaces[1]=row;seq["surfaces"]=surfaces;bad["sequential"]=seq;
    rejects([&]{deserializeProject(QJsonDocument(bad).toJson(),false);},"Self/future coordinate return rejected even in undo parsing");
    rejects([&]{exportGeopter(folded);},"Unsupported coordinate Geopter export cannot silently flatten geometry");
    auto configured=coordinateGroupProject();configured.optimization=defaultOptimization(configured.system,configured.catalog);
    auto restored=deserializeProject(serializeProject(configured));
    check(restored.optimization && restored.optimization->algorithm==OptimizationAlgorithm::Coordinate,"Format 9 reads a legacy coordinate-search plan without inventing LM metadata");
    configured.optimization->algorithm=OptimizationAlgorithm::LevenbergMarquardt;
    check(deserializeProject(serializeProject(configured)).optimization->algorithm==OptimizationAlgorithm::LevenbergMarquardt,"Format 9 preserves advanced optimizer metadata too");
    Project plain;plain.workspace["sequentialViews"]=QJsonArray{0};w.setProject(plain);
    const auto before=serializeProject(w.project());bool saved=false,preview=false,invalidStayed=false;
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=w.findChild<QDialog*>("coordinateDialog");if(!dialog)return;
        auto* table=dialog->findChild<QTableWidget*>("coordinateTable");
        qobject_cast<QComboBox*>(table->cellWidget(0,1))->setCurrentIndex(1);
        table->item(0,5)->setText("bad");dialog->findChild<QAbstractButton*>("coordinateSave")->click();
        invalidStayed=dialog->isVisible() && dialog->result()!=QDialog::Accepted;
        table->item(0,5)->setText("1,25");dialog->findChild<QAbstractButton*>("coordinatePreview")->click();
        preview=dialog->findChild<QPlainTextEdit*>("coordinateFrames")->toPlainText().contains("Изображение");
        if(!dir.isEmpty())dialog->grab().save(dir+"/coordinate_editor.png");
        dialog->findChild<QAbstractButton*>("coordinateSave")->click();saved=dialog->result()==QDialog::Accepted;if(!saved)dialog->reject();
    });
    w.findChild<QAction*>("coordinateAction")->trigger();check(saved&&preview&&invalidStayed,"Real coordinate editor validates numbers, previews global frames and saves");
    near(w.project().system.surfaces[0].coordinates.shift.y,1.25,1e-12,"Coordinate editor accepts comma input");
    w.findChild<QAction*>("undoAction")->trigger();check(serializeProject(w.project())==before,"Coordinate edit participates in full-project undo");
    w.findChild<QAction*>("redoAction")->trigger();near(w.project().system.surfaces[0].coordinates.shift.y,1.25,1e-12,"Coordinate redo restores inherited transform");
    for(const auto& project:{folded,coordinateGroupProject(),afocalTelescopeProject()}) {
        check(project.system.validate(project.catalog).empty(),"New physical example validates");
        w.setProject(project);w.recalculate();
        check(w.results()->error.isEmpty()&&!w.results()->spots.empty()&&!w.results()->spots[0].samples.empty(),"Folded, off-axis and afocal examples retain geometric analyses");
        const auto first=differentialFirstOrder(project.system,project.catalog,project.system.wavelengths[project.system.primary].um);
        if(project.system.name.find("Афокальный")!=std::string::npos) {
            check(first.afocal,"Thick-lens afocal telescope is classified correctly");
            near(first.matrix[10]/first.outputIndex,-2,1e-5,"Afocal telescope angular magnification matches independent design target");
        }
        w.findChild<QAction*>("firstOrderAction")->trigger();auto* report=w.findChild<QDialog*>("firstOrderDialog");
        check(report&&report->findChild<QPlainTextEdit*>("firstOrderText")->toPlainText().contains("Матрица"),"GUI first-order report displays actual 4x4 matrix");
        if(report){if(!dir.isEmpty())report->grab().save(dir+"/coordinate_first_order.png");report->close();QTest::qWait(1);}
        const auto request=QJsonDocument::fromJson(serializeProject(project)).object();
        const auto analysis=executeRequest({{"operation","analyze"},{"project",request},{"options",QJsonObject{{"pupil_grid",9}}}});
        check(!analysis["fields"].toArray().isEmpty(),"Batch analyze returns geometry even when scalar paraxial EFL is unavailable");
        const auto api=executeRequest({{"operation","first_order"},{"project",request}});
        near(api["matrix"].toArray()[2].toArray()[2].toDouble(),first.matrix[10],1e-10,"Batch first order agrees with core and GUI");
        PlotWidget layout(View::Layout);layout.resize(1000,600);layout.setData(w.results());layout.show();
        for(int projection=0;projection<3;++projection){layout.layoutProjection=projection;layout.update();QTest::qWait(20);const auto pixmap=layout.grab();check(!layout.axisViews().empty(),"All global layout projections produce real axes");if(!dir.isEmpty())pixmap.save(dir+QString("/coordinate_%1_%2.png").arg(int(project.system.surfaces.size())).arg(projection));}
    }
    w.setProject(folded);w.recalculate();check(!w.results()->waveError.isEmpty()&&!w.results()->diffractionError.isEmpty(),"Unvalidated folded wave/diffraction views have explicit diagnostics");
    PlotWidget* activeLayout=nullptr;for(auto* plot:w.findChildren<PlotWidget*>())if(plot->view==View::Layout)activeLayout=plot;
    check(activeLayout!=nullptr,"Main window provides a real layout view");
    auto* tabs=qobject_cast<QTabWidget*>(activeLayout->parentWidget()->parentWidget());check(tabs!=nullptr,"Layout belongs to analysis tabs");tabs->setCurrentWidget(activeLayout);
    auto* projection=w.findChild<QAction*>("layoutProjectionAction");check(projection&&projection->isVisible(),"Global projection command is actually reachable in the shared toolbar");
    projection->trigger();check(activeLayout->layoutProjection==1,"Shared toolbar changes current plot projection");
    activeLayout->resetView();check(activeLayout->layoutProjection==0,"Standard view restores the default projection");
    const auto request=QJsonDocument::fromJson(serializeProject(folded)).object();
    // Qt 6.8 may resolve a single nested QJsonArray initializer as a copy.
    // Append explicitly so the transport is [[0,0]] on all supported Qt versions.
    QJsonArray pupils;pupils.append(QJsonArray{0,0});
    check(pupils.size()==1 && pupils[0].isArray(),"Diagnostic pupil transport is a nested array on every supported Qt");
    auto rays=executeRequest({{"operation","trace_rays"},{"project",request},{"options",QJsonObject{{"field",0},{"pupils",pupils}}}})["rays"].toArray();
    check(rays[0].toObject()["complete"].toBool() && rays[0].toObject()["points_world_mm"].toArray().size()==4,"Batch diagnostic exposes true global folded intersections");
    near(rays[0].toObject()["image_mm"].toArray()[1].toDouble(),40,1e-8,"Batch world image is on the folded Y plane");
    w.setProject(original);return count;
}
