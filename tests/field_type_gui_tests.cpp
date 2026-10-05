#include "field_editor.hpp"
#include "window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <stdexcept>

using namespace optics;
size_t fieldTypeGUIChecks(Window& w, const QString& dir) {
    size_t checks=0;
    auto check=[&](bool ok,const char* message){++checks; if(!ok) throw std::runtime_error(message);};
    auto accept=[](FieldEditor& d){d.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();};
    const auto original=w.project();
    Project initial;
    FieldEditor d(initial);
    auto* type=d.findChild<QComboBox*>("fieldTypeCombo");
    auto* distance=d.findChild<QDoubleSpinBox*>("fieldObjectDistance");
    auto* table=d.findChild<QTableWidget*>("fieldTable");
    check(type && type->count()==4 && distance,"Field editor exposes four definitions and object distance");
    type->setCurrentIndex(1);
    check(table->horizontalHeaderItem(1)->text().contains("мм"),"Height selection updates visible coordinate units");
    accept(d);
    check(d.result()!=QDialog::Accepted && !d.findChild<QLabel*>("fieldMessage")->text().isEmpty(),"Object height at infinity shows inline error");
    distance->setValue(200);
    check(d.findChild<QLabel*>("fieldMessage")->text().isEmpty(),"Editing a field setting clears the previous inline error");
    table->item(1,1)->setText("-5"); table->item(2,1)->setText("-10"); table->item(1,5)->setText("0,2");
    d.show(); QTest::qWait(120);
    if(!dir.isEmpty()) d.grab().save(dir+"/object_height_editor.png");
    accept(d);
    check(d.result()==QDialog::Accepted,"Finite object-height fields accepted");
    auto p=d.project();
    check(p.system.fieldType==FieldType::ObjectHeight && p.system.objectDistance==200,"Editor commits type and object conjugate together");
    check(p.system.fields[1].y==-5 && p.system.fields[1].vuy==.2,"Coordinates and vignetting keep their values");
    check(initial.system.fieldType==FieldType::Angle && initial.system.objectDistance==0,"Editor does not mutate source project before commit");
    auto root=QJsonDocument::fromJson(serializeProject(p)).object();
    check(root["version"]==6 && root["sequential"].toObject()["fieldType"]==1,"Height fields use explicit metadata in format 6");
    auto restored=deserializeProject(QJsonDocument(root).toJson());
    check(restored.system.fieldType==FieldType::ObjectHeight && restored.system.fields[1].vuy==.2,"Format 6 roundtrips type and format-5 coefficients");
    for(int version : {1,2,3,4,5}) {
        auto disguised=root; disguised["version"]=version; bool rejected=false;
        try {deserializeProject(QJsonDocument(disguised).toJson(),false);} catch(const std::exception&){rejected=true;}
        check(rejected,"Height metadata cannot be disguised as any prior format, even without geometry validation");
    }
    for(auto value : {QJsonValue(-1),QJsonValue(3),QJsonValue(1.5),QJsonValue("bad"),QJsonValue(QJsonValue::Null)}) {
        auto invalid=root; auto seq=invalid["sequential"].toObject(); seq["fieldType"]=value; invalid["sequential"]=seq;
        bool rejected=false; try {deserializeProject(QJsonDocument(invalid).toJson(),false);} catch(const std::exception&){rejected=true;}
        check(rejected,"Unknown, fractional or nonnumeric field type rejected");
    }
    auto missing=root; auto seq=missing["sequential"].toObject(); seq.remove("fieldType"); missing["sequential"]=seq;
    bool rejected=false; try {deserializeProject(QJsonDocument(missing).toJson(),false);} catch(const std::exception&){rejected=true;}
    check(rejected,"Format 6 without field units is rejected");
    FieldEditor reopened(p);
    check(reopened.findChild<QComboBox*>("fieldTypeCombo")->currentIndex()==1,"Reopened editor restores field type");
    accept(reopened);
    check(serializeProject(reopened.project())==serializeProject(p),"Reopening and accepting preserves complete finite-field project");
    auto precise=p; precise.system.objectDistance=200.12345678901234;
    FieldEditor preciseEditor(precise); accept(preciseEditor);
    check(preciseEditor.project().system.objectDistance==precise.system.objectDistance,"Accepting an unchanged distance preserves all stored precision");
    FieldEditor image(p); image.findChild<QComboBox*>("fieldTypeCombo")->setCurrentIndex(2);
    image.findChild<QDoubleSpinBox*>("fieldObjectDistance")->setValue(0);
    auto* it=image.findChild<QTableWidget*>("fieldTable"); it->item(1,1)->setText("3"); it->item(2,1)->setText("5");
    accept(image);
    check(image.result()==QDialog::Accepted && image.project().system.fieldType==FieldType::ParaxialImageHeight,"Image-height fields work for an infinite object");
    check(deserializeProject(serializeProject(image.project())).system.fieldType==FieldType::ParaxialImageHeight,"Image-height type survives persistence");
    // Import uses Geopter's numeric field identifiers without changing their signs.
    QFile fixture(QString(OPTICS_SOURCE_DIR)+"/tests/data/geopter_singlet.json");
    check(fixture.open(QIODevice::ReadOnly),"Original synthetic interchange fixture available");
    const auto interchange=QJsonDocument::fromJson(fixture.readAll()).object();
    for(int kind : {1,2}) {
        auto input=interchange; auto spec=input["Spec"].toObject(); auto fields=spec["Field"].toObject();
        fields["Type"]=kind; fields["VUY"]=QJsonArray{0,.2}; spec["Field"]=fields; input["Spec"]=spec;
        auto assembly=input["Assembly"].toObject(); auto object=assembly["0"].toObject(); object["Thickness"]=200;
        assembly["0"]=object; input["Assembly"]=assembly;
        const auto imported=importGeopter(QJsonDocument(input).toJson());
        check(int(imported.system.fieldType)==kind && imported.system.objectDistance==200,"Geopter finite field type and conjugate imported");
        check(imported.system.fields[1].y==3 && imported.system.fields[1].vuy==.2,"Geopter height and factors retain sign and identity");
        auto ray=pupilRay(imported.system,imported.catalog,imported.system.fields[1],.5875618,0,0);
        check(kind==2 || std::abs(ray.origin.y-3)<1e-12,"Imported object height launches from physical 3 mm source");
        check(trace(imported.system,imported.catalog,ray).status==TraceStatus::Complete,"Imported height chief ray completes");
        check(QJsonDocument::fromJson(serializeProject(imported)).object()["version"]==6,"Imported height projects use format 6");
    }
    for(double kind : {-1.,.5,3.}) {
        auto input=interchange; auto spec=input["Spec"].toObject(); auto fields=spec["Field"].toObject(); fields["Type"]=kind;
        spec["Field"]=fields; input["Spec"]=spec; bool bad=false;
        try {importGeopter(QJsonDocument(input).toJson());} catch(const std::exception&){bad=true;}
        check(bad,"Unknown or fractional Geopter field identifier rejected");
    }
    auto infinity=interchange; auto spec=infinity["Spec"].toObject(); auto fields=spec["Field"].toObject(); fields["Type"]=1;
    spec["Field"]=fields; infinity["Spec"]=spec; rejected=false;
    try {importGeopter(QJsonDocument(infinity).toJson());} catch(const std::exception&){rejected=true;}
    check(rejected,"Imported object height with infinite conjugate rejected");
    w.setProject(initial);
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=qobject_cast<FieldEditor*>(QApplication::activeModalWidget()); if(!dialog) return;
        dialog->findChild<QComboBox*>("fieldTypeCombo")->setCurrentIndex(1);
        dialog->findChild<QDoubleSpinBox*>("fieldObjectDistance")->setValue(200); accept(*dialog);
    });
    w.findChild<QToolButton*>("fieldsButton")->click(); QTest::qWait(150);
    check(w.project().system.fieldType==FieldType::ObjectHeight,"Ribbon editor applies field type to actual window");
    check(w.findChild<QComboBox*>("fieldCombo")->itemText(1).contains("мм"),"Analysis field selector displays millimetres");
    check(w.results()->error.isEmpty() && w.results()->waveError.isEmpty(),"Actual geometry and wave recalculation succeeds");
    w.findChild<QAction*>("undoAction")->trigger();
    check(w.project().system.fieldType==FieldType::Angle && w.project().system.objectDistance==0,"Undo restores angular fields and previous conjugate");
    w.findChild<QAction*>("redoAction")->trigger();
    check(w.project().system.fieldType==FieldType::ObjectHeight && w.project().system.objectDistance==200,"Redo restores field type and conjugate");
    w.setProject(image.project()); w.recalculate(); QTest::qWait(180);
    check(w.results()->error.isEmpty() && w.results()->fanError.isEmpty() && w.results()->diffraction.psf.size()==4096,"Image-height GUI computes spot, ray fan and FFT");
    bool parametersOpened=false;
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget()); if(!dialog) return;
        auto* kind=dialog->findChild<QComboBox*>("parameterFieldType");
        parametersOpened=kind && kind->currentIndex()==2;
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    w.findChild<QToolButton*>("parametersButton")->click();
    check(parametersOpened && w.project().system.fieldType==FieldType::ParaxialImageHeight,"General parameters dialog also preserves image-height definition");
    if(!dir.isEmpty()) w.grab().save(dir+"/image_height_project.png");
    // Exercise actual CSV save dialog and units rather than a copied formatter.
    auto* rms=w.findChild<QToolButton*>("analysis_"+QString::number(int(View::RMS)));
    check(rms!=nullptr,"RMS command is available for height fields"); rms->click();
    auto* tabs=w.findChild<QTabWidget*>("analysisTabs");
    auto* active=qobject_cast<PlotWidget*>(tabs->currentWidget());
    check(active && active->view==View::RMS,"Actual RMS tab selected before exporting");
    QTemporaryDir temporary;
    const QString path=temporary.path()+"/height-rms.csv";
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); if(!dialog) return;
        dialog->selectFile(path); dialog->findChild<QLineEdit*>("fileNameEdit")->setText(path);
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    w.findChild<QToolButton*>("csvButton")->click();
    QFile output(path);
    check(output.open(QIODevice::ReadOnly),"RMS of height fields exports a real CSV");
    check(output.readAll().startsWith("field,x_mm,y_mm,RMS_um"),"Height-field RMS export uses mm rather than degrees");
    output.close(); output.remove();
    w.setProject(original);
    return checks;
}
