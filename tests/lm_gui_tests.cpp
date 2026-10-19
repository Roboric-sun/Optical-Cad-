#include "window.hpp"
#include "optimization_editor.hpp"
#include "automation.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <stdexcept>
using namespace optics;
size_t lmGUIChecks(Window& w,const QString& dir) {
    size_t count=0;const auto original=w.project();
    auto check=[&](bool ok,const char* text){++count;if(!ok)throw std::runtime_error(text);};
    auto near=[&](double a,double b,double eps,const char* text){check(std::isfinite(a)&&std::abs(a-b)<eps,text);};
    auto rejects=[&](auto fn,const char* text){bool bad=false;try{fn();}catch(const std::exception&){bad=true;}check(bad,text);};
    Project p;p.workspace["sequentialViews"]=QJsonArray{0};auto plan=defaultOptimization(p.system,p.catalog);plan.algorithm=OptimizationAlgorithm::LevenbergMarquardt;
    plan.pupilGrid=33;plan.starts=3;plan.refocus=true;plan.allFieldsFocus=true;
    plan.variables={{VariableParameter::Radius,0,40,70,1}};
    plan.operands={{MeritKind::WavefrontRMS,-1,0,.001,1},{MeritKind::MTFX,0,.5,1,1},
        {MeritKind::MTFY,0,.5,1,1},{MeritKind::Distortion,-1,0,1,1},
        {MeritKind::TotalTrack,-1,5,1,1},{MeritKind::EdgeThickness,-1,2,1,1}};
    plan.operands.back().height=5;plan.operands[1].frequency=12.5;plan.operands[2].frequency=12.5;
    p.optimization=plan;
    auto json=QJsonDocument::fromJson(serializeProject(p)).object();
    check(json["version"]==8,"DLS and advanced merit explicitly require format 8");
    auto restored=deserializeProject(QJsonDocument(json).toJson());
    check(serializeProject(restored)==serializeProject(p),"DLS plan, spectral focus and all operands roundtrip exactly");
    auto old=json;old["version"]=7;
    rejects([&]{deserializeProject(QJsonDocument(old).toJson(),false);},"Legacy formats cannot conceal new optimizer metadata");
    auto altered=json;auto settings=altered["optimization"].toObject();settings["algorithm"]=.5;altered["optimization"]=settings;
    rejects([&]{deserializeProject(QJsonDocument(altered).toJson());},"Fractional solver enum is rejected");
    settings=json["optimization"].toObject();settings["starts"]=0;altered["optimization"]=settings;
    rejects([&]{deserializeProject(QJsonDocument(altered).toJson());},"Zero multistart count is rejected when loading");
    settings=json["optimization"].toObject();settings.remove("allFieldsFocus");altered["optimization"]=settings;
    rejects([&]{deserializeProject(QJsonDocument(altered).toJson(),false);},"Format 8 does not guess missing focus metadata");
    // Format 8 remains a superset of the previous extended material/surface schema.
    auto extended=p;extended.system.surfaces[0].asphere[9]=1e-30;
    extended.catalog.importAGF("NM SCHOTT_DLS 1 0 1.5 60\nCD 2.25 0 0 0 0 0\nLD .4 .8\n");
    auto extra=deserializeProject(serializeProject(extended));
    check(extra.catalog.get("SCHOTT_DLS").schottFormula && extra.system.surfaces[0].asphere[9]==1e-30,"Format 8 preserves format 7 materials and high-order aspheres");
    auto exchange=importGeopter(exportGeopter(p));
    check(serializeProject(exchange)==serializeProject(p),"Native Geopter extension preserves DLS settings");
    Project legacy;legacy.optimization=defaultOptimization(legacy.system,legacy.catalog);
    check(QJsonDocument::fromJson(serializeProject(legacy)).object()["version"]==1,"Legacy coordinate plans keep their previous file version");
    w.setProject(p);w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);const auto before=serializeProject(w.project());
    bool saved=false,preview=false,controls=false;
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=w.findChild<QDialog*>("optimizationDialog");if(!dialog)return;
        auto* algorithm=dialog->findChild<QComboBox*>("optimizationAlgorithm");
        auto* starts=dialog->findChild<QSpinBox*>("optimizationStarts");
        auto* all=dialog->findChild<QCheckBox*>("optimizationAllFieldsFocus");
        auto* table=dialog->findChild<QTableWidget*>("meritOperands");
        controls=algorithm && starts && all && table->columnCount()==10;
        if(!controls){dialog->reject();return;}
        starts->setValue(2);table->item(1,7)->setText("10,25");table->item(5,9)->setText("4,5");
        dialog->findChild<QAbstractButton*>("evaluateMeritButton")->click();
        preview=table->item(0,5)->text()!="—" && dialog->findChild<QLabel*>("meritPreviewMessage")->text().contains("безразмерный");
        if(!dir.isEmpty()) {
            dialog->findChild<QTabWidget*>()->setCurrentIndex(1);
            dialog->grab().save(dir+"/dls_editor.png");
        }
        dialog->findChild<QAbstractButton*>("saveOptimizationButton")->click();saved=dialog->result()==QDialog::Accepted;
        if(!saved)dialog->reject();
    });
    w.findChild<QAbstractButton*>("meritEditorButton")->click();
    check(controls && preview && saved,"Real merit dialog exposes DLS and evaluates all advanced operands");
    check(w.project().optimization->algorithm==OptimizationAlgorithm::LevenbergMarquardt && w.project().optimization->starts==2,"Dialog saves selected algorithm and start count");
    near(w.project().optimization->operands[1].frequency,10.25,1e-12,"MTF frequency accepts decimal comma");
    near(w.project().optimization->operands[5].height,4.5,1e-12,"Edge-height cell accepts decimal comma");
    w.findChild<QAction*>("undoAction")->trigger();
    check(serializeProject(w.project())==before,"Undo restores the entire advanced optimization plan");
    w.findChild<QAction*>("redoAction")->trigger();check(w.project().optimization->starts==2,"Redo restores DLS settings");
    // Surface references follow surviving adjacent pairs, never a different gap.
    auto reindexed=p;reindexOptimization(reindexed,0,0,1);
    check(reindexed.optimization->operands.back().surface==1,"Inserting before an edge pair reindexes its criterion");
    reindexed=p;reindexOptimization(reindexed,1,0,1);
    check(reindexed.optimization->operands.size()==5,"Splitting an edge pair removes that criterion instead of retargeting it");
    reindexed=p;reindexOptimization(reindexed,1,1,0);
    check(reindexed.optimization->operands.size()==5,"Deleting either edge face removes the dependent criterion");
    // Run the actual asynchronous GUI worker on a known one-dimensional task.
    Project fast;fast.workspace["sequentialViews"]=QJsonArray{0};OptimizationPlan quick;quick.algorithm=OptimizationAlgorithm::LevenbergMarquardt;quick.starts=2;quick.minimumThroughput=0;
    quick.variables={{VariableParameter::Defocus,0,-5,5,.5}};
    quick.operands={{MeritKind::ImageDistance,-1,fast.system.imageZ()+1,1,1}};fast.optimization=quick;
    w.setProject(fast);w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    const auto baseline=serializeProject(w.project());
    w.findChild<QAbstractButton*>("customOptimizeButton")->click();
    check(QTest::qWaitFor([&]{return !w.optimizing();},15000),"DLS GUI worker finishes asynchronously");
    near(w.project().system.defocus,1,1e-6,"DLS GUI commits the expected physical solution");
    auto* report=w.findChild<QDialog*>("optimizationReportDialog");
    const auto text=report ? report->findChild<QPlainTextEdit*>("optimizationReportText")->toPlainText() : QString{};
    check(text.contains("Levenberg") && text.contains("Сравнение стартов") && text.contains("Завершение:"),"GUI report contains algorithm, start comparison and termination");
    if(report){if(!dir.isEmpty())report->grab().save(dir+"/dls_report.png");report->close();QApplication::processEvents();}
    w.findChild<QAction*>("undoAction")->trigger();check(serializeProject(w.project())==baseline,"Undo restores the project after DLS");
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    w.findChild<QAbstractButton*>("customOptimizeButton")->click();w.findChild<QAbstractButton*>("cancelOptimizationButton")->click();
    check(QTest::qWaitFor([&]{return !w.optimizing();},15000) && serializeProject(w.project())==baseline,"Ribbon cancellation leaves a DLS project unchanged");
    Project focus;auto expected=focus.system;autofocusAllFields(expected,focus.catalog,expected.pupilGrid);
    w.setProject(focus);w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    auto* focusButton=w.findChild<QAbstractButton*>("allFieldsFocusButton");check(focusButton!=nullptr,"All-field focus is a visible ribbon command");focusButton->click();
    near(w.project().system.imageZ(),expected.imageZ(),1e-9,"Ribbon common focus uses the shared core");
    w.findChild<QAction*>("undoAction")->trigger();near(w.project().system.imageZ(),focus.system.imageZ(),1e-12,"Common focus participates in undo");
    const auto request=QJsonDocument::fromJson(baseline).object();
    auto api=executeRequest({{"operation","optimize"},{"project",request}});
    check(api["algorithm"].toInt()==1 && api["starts"].toArray().size()==2 && api["after"].toDouble()<1e-6,"Batch/API uses DLS and returns per-start comparison");
    auto common=executeRequest({{"operation","autofocus"},{"project",QJsonDocument::fromJson(serializeProject(focus)).object()},
        {"options",QJsonObject{{"all_fields",true},{"pupil_grid",17}}}});
    near(common["image_z_mm"].toDouble(),expected.imageZ(),1e-9,"Common focus agrees in GUI and batch API");
    w.setProject(original);return count;
}
