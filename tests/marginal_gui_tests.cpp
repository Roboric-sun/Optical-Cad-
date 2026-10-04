#include "solve_editor.hpp"
#include "window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <stdexcept>

using namespace optics;
size_t marginalGUIChecks(Window& w, const QString& dir) {
    size_t checks=0;
    auto check=[&](bool ok,const char* msg){ ++checks; if(!ok) throw std::runtime_error(msg); };
    auto close=[&](double a,double b,const char* msg){ check(std::isfinite(a) && std::abs(a-b)<1e-7,msg); };
    const auto original=w.project();
    auto accept=[](SolveEditor& d){ d.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); };
    Project p;
    SolveEditor editor(p,1,SolveParameter::Thickness);
    auto* type=editor.findChild<QComboBox*>("solveType");
    check(type->findData(4)==4,"Marginal height appends a thickness solve without changing old choices");
    type->setCurrentIndex(type->findData(4));
    editor.findChild<QDoubleSpinBox*>("solveValue")->setValue(0);
    editor.findChild<QDoubleSpinBox*>("solvePupil")->setValue(.8);
    editor.show(); QTest::qWait(60);
    check(editor.findChild<QComboBox*>("solveField")->isVisible() &&
          editor.findChild<QComboBox*>("solveWavelength")->isVisible(),"Ray field and wavelength are visible in solve editor");
    check(editor.findChild<QDoubleSpinBox*>("solveValue")->minimum()<0,"Target height accepts signed values");
    if(!dir.isEmpty()) editor.grab().save(dir+"/marginal_height.png");
    accept(editor);
    check(editor.result()==QDialog::Accepted,"Valid ray-height condition accepted");
    p.system=editor.system();
    const auto& a=p.system.solves[0];
    check(a.kind==SolveKind::MarginalHeight && a.reference==2 && a.pupil==.8 && a.wavelength==SIZE_MAX,"Ray selection metadata committed");
    auto height=[&](const Project& project){
        const auto& rule=project.system.solves[0];
        const size_t wave=rule.wavelength==SIZE_MAX?project.system.primary:rule.wavelength;
        const auto ray=pupilRay(project.system,project.catalog,project.system.fields[rule.field],project.system.wavelengths[wave].um,0,rule.pupil);
        const auto path=trace(project.system,project.catalog,ray);
        check(path.status==TraceStatus::Complete,"Selected GUI ray completes its trace");
        return path.image.y;
    };
    close(height(p),0,"GUI solve reaches zero target height");
    auto bytes=serializeProject(p);
    auto root=QJsonDocument::fromJson(bytes).object();
    check(root["version"].toInt()==4,"Ray-dependent projects require format 4");
    auto loaded=deserializeProject(bytes);
    close(height(loaded),0,"Ray target survives JSON roundtrip");
    check(loaded.system.solves[0].wavelength==SIZE_MAX && loaded.system.solves[0].pupil==.8,"Primary sentinel and normalized pupil survive persistence");
    for(int version : {1,2,3}) {
        auto old=root; old["version"]=version;
        bool failed=false;
        try { deserializeProject(QJsonDocument(old).toJson()); } catch(const std::exception&){ failed=true; }
        check(failed,"Ray metadata cannot be disguised as an older format");
    }
    auto bad=root; auto seq=bad["sequential"].toObject(); auto rules=seq["solves"].toArray(); auto j=rules[0].toObject();
    j.remove("pupil"); rules[0]=j; seq["solves"]=rules; bad["sequential"]=seq;
    bool failed=false;
    try { deserializeProject(QJsonDocument(bad).toJson()); } catch(const std::exception&){ failed=true; }
    check(failed,"Missing ray selection metadata is rejected");
    auto stale=p; stale.system.surfaces[1].thickness=999;
    QTemporaryDir tmp;
    const auto path=tmp.path()+"/ray.optcad";
    saveProject(path,stale);
    check(stale.system.surfaces[1].thickness==999,"Saving resolves ray geometry in a separate copy");
    close(loadProject(path).system.surfaces[1].thickness,p.system.surfaces[1].thickness,"Saved ray geometry refreshes stale caches");
    SolveEditor reopened(p,1,SolveParameter::Thickness);
    check(reopened.findChild<QComboBox*>("solveType")->currentData().toInt()==4 &&
          reopened.findChild<QDoubleSpinBox*>("solvePupil")->value()==.8,"Reopening displays the selected ray");
    reopened.findChild<QDoubleSpinBox*>("solveValue")->setValue(100);
    accept(reopened);
    check(reopened.result()!=QDialog::Accepted && !reopened.findChild<QLabel*>("solveMessage")->text().isEmpty(),"Unreachable height shows an inline error without accepting");
    reopened.reject();
    w.setProject(p);
    auto* table=w.findChild<QTableWidget*>("surfaceTable");
    check(!(table->item(2,4)->flags() & Qt::ItemIsEditable) && !table->item(2,4)->icon().isNull(),"Ray-controlled thickness is protected and marked in table");
    table->item(1,3)->setText("60"); QTest::qWait(200);
    close(height(w.project()),0,"Editing an upstream radius updates the constrained image gap");
    const double changed=w.project().system.surfaces[1].thickness;
    w.findChild<QAction*>("undoAction")->trigger(); QTest::qWait(200);
    close(w.project().system.surfaces[1].thickness,p.system.surfaces[1].thickness,"Undo restores ray geometry");
    w.findChild<QAction*>("redoAction")->trigger(); QTest::qWait(200);
    close(w.project().system.surfaces[1].thickness,changed,"Redo restores the updated ray condition");
    w.recalculate(); QApplication::processEvents();
    check(w.results() && w.results()->error.isEmpty() && !w.results()->spots.empty(),"All standard analyses calculate with the new solve");
    if(!dir.isEmpty()) w.grab().save(dir+"/marginal_project.png");
    // Exercise the actual context menu, including removal through the fixed-value choice.
    bool menuFound=false, dialogFound=false;
    table->setCurrentCell(2,4);
    QTimer::singleShot(0,&w,[&]{
        auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if(!menu) return;
        for(auto* action:menu->actions()) if(action->text()=="Расчёт толщины…") {
            menuFound=true;
            QTimer::singleShot(0,&w,[&]{
                auto* dialog=qobject_cast<SolveEditor*>(QApplication::activeModalWidget());
                if(!dialog) return;
                dialogFound=true;
                dialog->findChild<QComboBox*>("solveType")->setCurrentIndex(0);
                accept(*dialog);
            });
            QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());
            return;
        }
        menu->close();
    });
    QMetaObject::invokeMethod(table,"customContextMenuRequested",Qt::DirectConnection,
        Q_ARG(QPoint,table->visualItemRect(table->item(2,4)).center()));
    check(menuFound && dialogFound && w.project().system.solves.empty(),"Context editor removes the ray condition through fixed value");
    check(table->item(2,4)->flags() & Qt::ItemIsEditable,"Removing ray condition restores manual thickness editing");
    // Actual user catalog must drive solving after persistence, including built-in-name overrides.
    Material custom; custom.name="N-BK7"; custom.nd=1.7;
    p.catalog.add(custom); applySolves(p.system,p.catalog);
    const double customGap=p.system.surfaces[1].thickness;
    loaded=deserializeProject(serializeProject(p));
    close(loaded.system.surfaces[1].thickness,customGap,"Loading ray solves uses project catalog overrides");
    check(std::abs(customGap-changed)>1,"User material changes the solved distance");
    QFile reference(QString(OPTICS_SOURCE_DIR) + "/examples/geopter/kingslake_doublet.json");
    check(reference.open(QIODevice::ReadOnly),"Pinned Geopter numerical example is available");
    const auto input=reference.readAll();
    auto doublet=importGeopter(input,Catalog{});
    check(doublet.system.surfaces.size()==4 && doublet.system.stop==0 && doublet.system.solves.empty(),
          "Original doublet imports endpoints and STOP without inventing absent solves");
    // Constants generated by scripts/reference_doublet.py, independent of this C++ kernel.
    const auto pa=paraxial(doublet.system,doublet.catalog,.587562);
    const double matrix[]={.940486474482322,.9390066114629176,-.08333330022632307,.9800773377839621};
    for(size_t i=0;i<4;++i) close(pa.matrix[i],matrix[i],"Imported doublet ABCD agrees with independent matrix product");
    close(pa.efl,12.000004767411372,"Imported doublet EFL agrees with independent reference");
    close(pa.bfl,11.285842177473778,"Imported doublet BFL agrees with independent reference");
    auto dr=pupilRay(doublet.system,doublet.catalog,doublet.system.fields[0],.587562,0,1);
    const auto dp=trace(doublet.system,doublet.catalog,dr);
    check(dp.status==TraceStatus::Complete,"Imported doublet marginal ray reaches image");
    const double hits[][2]={{2,0},{2,.2758012258038365},{1.9631810561312215,.6634416815299204},
                           {1.9274790707395721,1.3350861221308026}};
    for(size_t i=0;i<4;++i) {
        close(dp.points[i+1].y,hits[i][0],"Imported doublet intersection Y agrees with analytic sphere/Snell trace");
        close(dp.points[i+1].z,hits[i][1],"Imported doublet intersection Z agrees with analytic sphere/Snell trace");
    }
    close(dp.image.y,-.0010309782751234398,"Imported doublet image height agrees with independent trace");
    close(dp.opl,31.975603704823218,"Imported doublet OPL agrees with independent optical distances");
    auto extension=QJsonDocument::fromJson(input).object();
    extension["Solves"]=QJsonArray{QJsonObject{{"kind",4}}};
    failed=false; try { importGeopter(QJsonDocument(extension).toJson(),Catalog{}); } catch(const std::exception&){ failed=true; }
    check(failed,"Unknown Geopter root solve extension cannot lose its metadata silently");
    extension=QJsonDocument::fromJson(input).object();
    auto assembly=extension["Assembly"].toObject(); auto row=assembly["2"].toObject();
    row["Solve"]=QJsonObject{{"kind",4}}; assembly["2"]=row; extension["Assembly"]=assembly;
    failed=false; try { importGeopter(QJsonDocument(extension).toJson(),Catalog{}); } catch(const std::exception&){ failed=true; }
    check(failed,"Unknown per-surface Geopter solve extension cannot lose its metadata silently");
    ParameterSolve focus;
    focus.parameter=SolveParameter::Thickness; focus.kind=SolveKind::MarginalHeight;
    focus.surface=3; focus.reference=4; focus.value=0;
    doublet.system.solves={focus}; applySolves(doublet.system,doublet.catalog);
    const auto focused=trace(doublet.system,doublet.catalog,dr);
    close(focused.image.y,0,"Ray solve focuses the imported real doublet without changing lens surfaces");
    close(focused.points[4].z,dp.points[4].z,"Image solve preserves physical doublet geometry");
    Project large;
    large.system.surfaces.assign(500,Surface{});
    for(auto& surface:large.system.surfaces) surface.thickness=.1;
    large.system.surfaces.back().radius=50;
    large.system.surfaces.back().material="N-BK7";
    auto limitRule=focus; limitRule.surface=499; limitRule.reference=500;
    large.system.solves={limitRule}; applySolves(large.system,large.catalog);
    const auto largeLoaded=deserializeProject(serializeProject(large));
    check(largeLoaded.system.solves[0].reference==500 && largeLoaded.system.surfaces.size()==500,
          "Ray solve project roundtrips at the 500-surface limit");
    const auto limitRay=pupilRay(largeLoaded.system,largeLoaded.catalog,largeLoaded.system.fields[0],
                               largeLoaded.system.wavelengths[largeLoaded.system.primary].um,0,1);
    close(trace(largeLoaded.system,largeLoaded.catalog,limitRay).image.y,0,"Maximum-size loaded project preserves the ray target");
    // Image endpoint is index 500 at the supported 500-surface limit.
    auto endpoint=QJsonDocument::fromJson(serializeProject(p)).object();
    auto endpointSeq=endpoint["sequential"].toObject();
    auto endpointRules=endpointSeq["solves"].toArray();
    auto endpointRule=endpointRules[0].toObject();
    endpointRule["surface"]=499; endpointRule["reference"]=500;
    endpointRules[0]=endpointRule; endpointSeq["solves"]=endpointRules; endpoint["sequential"]=endpointSeq;
    const auto snapshot=deserializeProject(QJsonDocument(endpoint).toJson(),false);
    check(snapshot.system.solves[0].reference==500,"Undo parser preserves the maximum supported image endpoint");
    failed=false;
    try { deserializeProject(QJsonDocument(endpoint).toJson()); } catch(const std::exception&){ failed=true; }
    check(failed,"Normal loading still validates endpoint against actual surface count");
    w.setProject(original);
    return checks;
}
