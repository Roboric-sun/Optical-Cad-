#include "window.hpp"
#include "automation.hpp"
#include "python_console.hpp"
#include <QAction>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTest>
#include <QTableWidget>
#include <stdexcept>
using namespace optics;
size_t releaseGUIChecks(Window& window,const QString& dir) {
    size_t count=0;
    auto check=[&](bool condition,const char* msg){++count;if(!condition)throw std::runtime_error(msg);};
    auto rejects=[&](auto fn,const char* msg){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed,msg);};
    Project p;p.system.surfaces[0].asphere[9]=1e-30;p.system.surfaces[1].oddAsphere[0]=1e-7;
    auto doc=QJsonDocument::fromJson(serializeProject(p)).object();
    check(doc["version"]==7,"High and odd aspheres require format 7");
    auto restored=deserializeProject(QJsonDocument(doc).toJson());
    check(restored.system.surfaces[0].asphere[9]==1e-30 && restored.system.surfaces[1].oddAsphere[0]==1e-7,"Extended coefficients roundtrip exactly");
    auto old=doc;old["version"]=6;
    rejects([&]{deserializeProject(QJsonDocument(old).toJson(),false);},"Older formats cannot hide high/odd coefficients");
    p.system.surfaces[0].asphere[9]=0;
    p.system.solves={{SolveParameter::Radius,1,SolveKind::Pickup,0,-1,0}};
    auto geopter=exportGeopter(p);
    auto imported=importGeopter(geopter);
    check(serializeProject(imported)==serializeProject(p),"Geopter native extension preserves project and live solves");
    auto edited=QJsonDocument::fromJson(geopter).object();auto assembly=edited["Assembly"].toObject();auto row=assembly["1"].toObject();row["Curvature"]=.01;assembly["1"]=row;edited["Assembly"]=assembly;
    rejects([&]{importGeopter(QJsonDocument(edited).toJson());},"Stale exchange extension cannot override edited geometry");
    edited.remove("OpticalCADExtension");
    auto geometry=importGeopter(QJsonDocument(edited).toJson());
    check(geometry.system.surfaces[0].radius==100 && geometry.system.solves.empty(),"Explicit plain Geopter import reads edited numeric geometry");
    check(geometry.system.surfaces[1].oddAsphere[0]==1e-7,"ODD profile imports radial A3");
    auto malformed=edited;auto spec=malformed["Spec"].toObject();auto pupil=spec["Pupil"].toObject();pupil["Type"]=.5;spec["Pupil"]=pupil;malformed["Spec"]=spec;
    rejects([&]{importGeopter(QJsonDocument(malformed).toJson());},"Fractional Geopter aperture enum is rejected");
    auto finite=p;finite.system.solves.clear();finite.system.objectDistance=200;finite.system.stop=1;
    auto finitePlain=importGeopter(exportGeopter(finite,false));
    check(std::abs(finitePlain.system.pupilDiameter-finite.system.pupilDiameter)<1e-12,"Finite EPD export/import converts pupil planes reversibly");
    auto optimized=Project{};auto plan=defaultOptimization(optimized.system,optimized.catalog);
    plan.variables={{VariableParameter::A22,0,-1e-28,1e-28,1e-30}};optimized.optimization=plan;
    auto planRoundtrip=deserializeProject(serializeProject(optimized));
    check(planRoundtrip.optimization->variables[0].parameter==VariableParameter::A22,"High-order optimization variable survives format 7");
    Project schott;schott.catalog.importAGF("NM SCHOTT_TEST 1 0 1.5 60\nCD 2.25 0 0 0 0 0\nLD .4 .8\n");
    auto saved=deserializeProject(serializeProject(schott));
    check(saved.catalog.get("SCHOTT_TEST").schottFormula && saved.catalog.get("SCHOTT_TEST").index(.55)==1.5,"Schott formula survives project storage");
    p=Project{};p.system.fieldType=FieldType::RealImageHeight;p.system.fields={{1,2,1}};
    restored=deserializeProject(serializeProject(p));check(restored.system.fieldType==FieldType::RealImageHeight,"Real height field uses format 7");
    rejects([&]{exportGeopter(p);},"Unsupported real height cannot silently change during Geopter export");
    p=Project{};const auto sp=spot(p.system,p.catalog,p.system.fields[0],9);
    const auto api=executeRequest({{"operation","analyze"},{"project",QJsonDocument::fromJson(serializeProject(p)).object()},{"options",QJsonObject{{"pupil_grid",9}}}});
    check(std::abs(api["fields"].toArray()[0].toObject()["rms_mm"].toDouble()-sp.rms)<1e-14,"Batch operation exactly matches core analysis");
    window.setProject(p);
    check(window.findChild<QAction*>("pythonConsoleAction") && window.findChild<QAction*>("polychromaticAction") && window.findChild<QAction*>("exportGeopterAction"),"New features have real GUI actions");
    auto* table=window.findChild<QTableWidget*>("surfaceTable");
    check(table && table->columnCount()==36,"GUI exposes all twenty polynomial coefficients");
    table->item(1,25)->setText("1e-30");
    check(window.project().system.surfaces[0].asphere[9]==1e-30,"Editing A22 changes physical model");
    window.findChild<QAction*>("undoAction")->trigger();
    check(window.project().system.surfaces[0].asphere[9]==0,"Undo restores high-order coefficient");
    table->item(2,26)->setText("1e-7");
    check(window.project().system.surfaces[1].oddAsphere[0]==1e-7,"Editing A3 changes physical model");
    window.setProject(p);
    if(!QStandardPaths::findExecutable("python3").isEmpty() || !QStandardPaths::findExecutable("python").isEmpty()) {
        bool applied=false;
        PythonConsole console(p,[&](Project result){applied=true;p=std::move(result);});
        auto* source=console.findChild<QPlainTextEdit*>("pythonSource");
        auto* run=console.findChild<QPushButton*>("pythonRun");
        auto* stop=console.findChild<QPushButton*>("pythonStop");
        auto wait=[&]{QElapsedTimer clock;clock.start();while(!run->isEnabled()&&clock.elapsed()<15000)QTest::qWait(20);check(run->isEnabled(),"Python process finishes without blocking GUI");};
        source->setPlainText("project.system['surfaces'][0]['radius'] = 61\nprint(project.analyze()['efl_mm'])");run->click();wait();
        check(applied && p.system.surfaces[0].radius==61,"Python console applies valid project after successful execution");
        if(!dir.isEmpty()){console.show();QTest::qWait(80);console.grab().save(dir+"/python_console.png");}
        applied=false;source->setPlainText("project.system['surfaces'][0]['radius'] = 70\nraise RuntimeError('test rollback')");run->click();wait();
        check(!applied && p.system.surfaces[0].radius==61,"Python exception leaves project unchanged");
        source->setPlainText("import time\ntime.sleep(30)");run->click();QTest::qWait(200);stop->click();wait();
        check(!applied,"Stopping Python prevents project commit");
    }
    Project poly;poly.system.pupilDiameter=.5;poly.workspace["polychromaticDiffraction"]=true;
    window.setProject(poly);window.recalculate();
    check(window.results()->diffractionError.isEmpty() && !window.results()->diffraction.psf.empty(),"GUI computes spectral PSF through shared core");
    if(!dir.isEmpty())window.grab().save(dir+"/release_spectral.png");
    window.setProject(Project{});
    return count;
}
