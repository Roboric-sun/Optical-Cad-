#include "field_editor.hpp"
#include "window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <stdexcept>

using namespace optics;
size_t fieldGUIChecks(Window& w,const QString& dir) {
    size_t checks=0;
    auto check=[&](bool ok,const char* m){++checks; if(!ok) throw std::runtime_error(m);};
    auto close=[&](double a,double b,const char* m){check(std::isfinite(a)&&std::abs(a-b)<1e-9,m);};
    const auto original=w.project();
    auto accept=[](FieldEditor& d){ d.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click(); };
    Project p;
    FieldEditor dialog(p);
    auto* table=dialog.findChild<QTableWidget*>("fieldTable");
    check(table && table->columnCount()==7 && table->rowCount()==3,"Structured field table exposes angles, weight and four factors");
    table->item(1,5)->setText("0,2"); table->item(1,6)->setText("0.25");
    dialog.show(); QTest::qWait(60);
    if(!dir.isEmpty()) dialog.grab().save(dir+"/field_editor.png");
    accept(dialog);
    check(dialog.result()==QDialog::Accepted,"Valid fractional factors accepted in editor");
    p=dialog.project(); close(p.system.fields[1].vuy,.2,"Positive-Y field factor committed");
    close(p.system.fields[1].vly,.25,"Negative-Y field factor committed");
    auto encoded=serializeProject(p); auto root=QJsonDocument::fromJson(encoded).object();
    check(root["version"]==5,"Nonzero field vignetting requires format 5");
    check(deserializeProject(encoded).system.fields[1].vly==.25,"Vignetting survives project roundtrip");
    for(int version : {1,2,3,4}) {
        auto disguised=root; disguised["version"]=version; bool failed=false;
        try { deserializeProject(QJsonDocument(disguised).toJson()); } catch(const std::exception&){failed=true;}
        check(failed,"Vignetting cannot be hidden under an older format number");
    }
    check(QJsonDocument::fromJson(serializeProject(Project{})).object()["version"]==1,"Zero-factor default projects retain format 1");
    FieldEditor invalid(p); auto* bad=invalid.findChild<QTableWidget*>("fieldTable");
    bad->item(1,5)->setText("1"); accept(invalid);
    check(invalid.result()!=QDialog::Accepted && !invalid.findChild<QLabel*>("fieldMessage")->text().isEmpty(),"Degenerate factor shows inline error without accepting");
    bad->item(1,5)->setText("text"); accept(invalid);
    check(invalid.result()!=QDialog::Accepted,"Nonnumeric factor rejected"); invalid.reject();
    // Reordering must carry the identity of referenced fields, rather than redirecting their indexes.
    ParameterSolve solve; solve.kind=SolveKind::MarginalHeight; solve.parameter=SolveParameter::Thickness;
    solve.surface=1; solve.reference=2; solve.field=1; solve.value=3;
    p.system.solves={solve}; applySolves(p.system,p.catalog);
    auto plan=defaultOptimization(p.system,p.catalog); plan.refocus=false;
    plan.operands={{MeritKind::SpotRMS,1,0,1,1}}; p.optimization=plan;
    FieldEditor reordered(p); auto* rt=reordered.findChild<QTableWidget*>("fieldTable");
    rt->setCurrentCell(1,0); reordered.findChild<QPushButton*>("fieldDown")->click(); accept(reordered);
    check(reordered.result()==QDialog::Accepted,"Referenced field can move while preserving identity");
    check(reordered.project().system.solves[0].field==2 && reordered.project().optimization->operands[0].field==2,
          "Ray solve and merit operand both follow reordered field");
    close(reordered.project().system.fields[2].vuy,.2,"Field movement preserves all coefficients");
    FieldEditor removed(p); auto* rem=removed.findChild<QTableWidget*>("fieldTable"); rem->setCurrentCell(1,0);
    removed.findChild<QPushButton*>("fieldRemove")->click(); accept(removed);
    check(removed.result()!=QDialog::Accepted && removed.findChild<QLabel*>("fieldMessage")->text().contains("используется"),"Deleting a referenced field does not silently redirect conditions"); removed.reject();
    FieldEditor single(Project{}); auto* st=single.findChild<QTableWidget*>("fieldTable");
    st->setCurrentCell(2,0); single.findChild<QPushButton*>("fieldRemove")->click();
    st->setCurrentCell(1,0); single.findChild<QPushButton*>("fieldRemove")->click();
    single.findChild<QPushButton*>("fieldRemove")->click();
    check(st->rowCount()==1,"Editor preserves at least one field"); single.reject();
    w.setProject(Project{});
    bool found=false;
    QTimer::singleShot(0,&w,[&]{
        auto* d=qobject_cast<FieldEditor*>(QApplication::activeModalWidget()); if(!d) return;
        found=true; d->findChild<QTableWidget*>("fieldTable")->item(1,5)->setText("0.3"); accept(*d);
    });
    auto* button=w.findChild<QToolButton*>("fieldsButton");
    check(button!=nullptr,"Ribbon has a dedicated field editor command"); button->click(); QTest::qWait(200);
    check(found && w.project().system.fields[1].vuy==.3,"Ribbon opens editor and commits a real model change");
    w.findChild<QAction*>("undoAction")->trigger(); QTest::qWait(150);
    close(w.project().system.fields[1].vuy,0,"Undo restores the full pupil");
    w.findChild<QAction*>("redoAction")->trigger(); QTest::qWait(150);
    close(w.project().system.fields[1].vuy,.3,"Redo restores vignetting");
    w.findChild<QComboBox*>("fieldCombo")->setCurrentIndex(1); w.recalculate();
    check(w.results()->error.isEmpty() && w.results()->waveError.isEmpty() && !w.results()->diffraction.psf.empty(),"Geometry and wave analyses work with edited vignetting");
    // Open a previously unsupported original Geopter system with all factors intact.
    QFile file(QString(OPTICS_SOURCE_DIR)+"/examples/geopter/dbgauss.json");
    check(file.open(QIODevice::ReadOnly),"Pinned Double Gauss fixture is available"); const auto input=file.readAll();
    auto gauss=importGeopter(input);
    check(gauss.system.surfaces.size()==11 && gauss.system.stop==5,"Double Gauss imports its physical surfaces and internal STOP");
    close(gauss.system.fields[1].vuy,.2,"Double Gauss VUY imported without loss");
    close(gauss.system.fields[1].vly,.25,"Double Gauss VLY imported without loss");
    close(gauss.system.fields[2].vuy,.4,"Double Gauss outer-field factor imported");
    check(gauss.system.validate(gauss.catalog).empty(),"Imported Double Gauss geometry is valid");
    auto prefix=gauss.system; prefix.surfaces.resize(prefix.stop+1);
    const double wave=gauss.system.wavelengths[gauss.system.primary].um;
    const double physicalStopRadius=std::abs(paraxial(prefix,gauss.catalog,wave).matrix[0])*gauss.system.pupilDiameter/2;
    for(auto field:gauss.system.fields)
        for(auto pupil:std::vector<Vec3>{{0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},
                                        {.6,.8,0},{-.6,.8,0},{.6,-.8,0},{-.6,-.8,0}}) {
            const auto ray=pupilRay(gauss.system,gauss.catalog,field,wave,pupil.x,pupil.y);
            const auto stopTrace=trace(gauss.system,gauss.catalog,ray,false,gauss.system.stop,false);
            check(stopTrace.status==TraceStatus::Complete,"Double Gauss chief and pupil edges reach its internal STOP");
            const auto desired=vignettedPupil(field,pupil.x,pupil.y)*physicalStopRadius;
            check(std::hypot(stopTrace.exitPoint.x-desired.x,stopTrace.exitPoint.y-desired.y)<1e-7,
                  "Double Gauss STOP aiming converges to each mapped pupil target");
        }
    auto g=QJsonDocument::fromJson(input).object(); auto spec=g["Spec"].toObject(); auto fields=spec["Field"].toObject();
    for(auto broken : {QJsonValue(QJsonArray{0,.2}),QJsonValue(QJsonArray{0,1,.4}),QJsonValue(QJsonArray{0,"bad",.4}),QJsonValue("bad")}) {
        fields["VUY"]=broken; spec["Field"]=fields; g["Spec"]=spec; bool failed=false;
        try { importGeopter(QJsonDocument(g).toJson()); } catch(const std::exception&){failed=true;}
        check(failed,"Malformed Geopter vignetting is rejected instead of discarded");
    }
    w.setProject(gauss); w.findChild<QComboBox*>("fieldCombo")->setCurrentIndex(2); w.recalculate();
    check(w.results()->error.isEmpty() && w.results()->spots.size()==3 && !w.results()->spots[2].samples.empty(),"Imported outer field produces real spot samples");
    QTest::qWait(200);
    if(!dir.isEmpty()) w.grab().save(dir+"/double_gauss.png");
    // Cancel and stale modal editing must not overwrite work performed while the dialog is open.
    const auto before=serializeProject(w.project());
    QTimer::singleShot(0,&w,[&]{auto* d=qobject_cast<FieldEditor*>(QApplication::activeModalWidget()); if(d) d->reject();});
    button=w.findChild<QToolButton*>("fieldsButton");
    button->click(); check(serializeProject(w.project())==before,"Cancelling field editor preserves project");
    Project replacement; replacement.system.name="Changed while field dialog was open";
    QTimer::singleShot(0,&w,[&]{
        auto* d=qobject_cast<FieldEditor*>(QApplication::activeModalWidget()); if(!d) return;
        w.setProject(replacement);
        QTimer::singleShot(0,&w,[]{
            if(auto* warning=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) warning->accept();
        });
        accept(*d);
    });
    button=w.findChild<QToolButton*>("fieldsButton"); button->click();
    check(w.project().system.name==replacement.system.name,"Stale field dialog cannot overwrite a newer project");
    w.setProject(original);
    return checks;
}
