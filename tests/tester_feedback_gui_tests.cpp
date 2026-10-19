#include "window.hpp"
#include "number_input.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QWheelEvent>
#include <stdexcept>

using namespace optics;
size_t testerFeedbackGUIChecks(Window& w,const QString& dir) {
    size_t checks=0;
    auto check=[&](bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);};
    auto near=[&](double a,double b,double tolerance,const char* message){check(std::isfinite(a)&&std::abs(a-b)<tolerance,message);};
    const auto original=w.project();
    const QLocale initialLocale;
    for(auto locale : {QLocale(QLocale::English),QLocale(QLocale::Russian)}) {
        QLocale::setDefault(locale);
        DecimalSpinBox spin;spin.setRange(-100,100);spin.setDecimals(6);spin.show();
        check(spin.locale()==QLocale::c(),"Numeric presentation is consistent across English and Russian locales");
        for(auto text : {QString("1,25"),QString("1.25"),QString("-0,125"),QString("-0.125")}) {
            auto* edit=spin.findChild<QLineEdit*>();edit->selectAll();QTest::keyClicks(edit,text);spin.interpretText();
            near(spin.value(),text.startsWith('-')?-.125:1.25,1e-12,"Real spin edit accepts decimal comma and point");
        }
    }
    QLocale::setDefault(initialLocale);
    for(auto text : {"bad","1,2.3","nan","inf"}) {bool ok=true;uiNumber(text,&ok);check(!ok,"Invalid numeric text is not silently coerced");}
    w.setProject(Project{});
    auto* table=w.findChild<QTableWidget*>("surfaceTable");
    table->item(1,3)->setText("50,5");near(w.project().system.surfaces[0].radius,50.5,1e-12,"Surface table accepts comma");
    table->item(1,3)->setText("50.25");near(w.project().system.surfaces[0].radius,50.25,1e-12,"Surface table accepts point");
    w.setProject(Project{});
    bool parameters=false;
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        auto* fields=dialog->findChild<QTextEdit*>("parameterFields");
        auto* waves=dialog->findChild<QTextEdit*>("parameterWavelengths");
        parameters=fields&&waves;
        if(parameters){fields->setPlainText("0 0 1\n0 2,5 1");waves->setPlainText("0,4861327 1\n0.5875618 1\n0,6562725 1");}
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    w.findChild<QToolButton*>("parametersButton")->click();
    check(parameters,"General system editor exposes field and wavelength input");
    near(w.project().system.fields[1].y,2.5,1e-12,"Field text rows accept decimal comma");
    near(w.project().system.wavelengths[0].um,.4861327,1e-12,"Wavelength text rows accept decimal comma");
    w.setProject(Project{});
    const auto savedPalette=QApplication::palette();
    for(bool dark : {false,true}) {
        auto palette=savedPalette;
        for(auto group : {QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
            palette.setColor(group,QPalette::Text,dark?Qt::white:Qt::black);
            palette.setColor(group,QPalette::WindowText,dark?Qt::white:Qt::black);
            palette.setColor(group,QPalette::Base,dark?QColor("#242424"):QColor(Qt::white));
        }
        QApplication::setPalette(palette);w.setPalette(palette);QTest::qWait(100);
        table->setCurrentCell(1,3);
        auto* item=table->item(2,1);table->scrollToItem(item);
        const QRect logical=table->visualItemRect(item).adjusted(4,4,-4,-4);
        const QImage image=table->viewport()->grab().toImage();
        const double ratio=image.devicePixelRatio();int ink=0;
        for(int y=qRound(logical.top()*ratio);y<qRound(logical.bottom()*ratio);++y)
            for(int x=qRound(logical.left()*ratio);x<qRound(logical.right()*ratio);++x)
                if(qGray(image.pixel(x,y))<140)++ink;
        check(ink>25,"Unselected table text is really painted dark under both system palettes");
        auto* tree=w.findChild<QTreeWidget*>();
        check(tree && tree->palette().color(QPalette::Text).lightness()<150,"Navigator text stays dark in an inherited dark theme");
        if(!dir.isEmpty())w.grab().save(dir+(dark?"/feedback_dark.png":"/feedback_light.png"));
    }
    QApplication::setPalette(savedPalette);w.setPalette(savedPalette);
    auto* scroll=w.findChild<QScrollArea*>("ribbonScroll");
    for(auto* button : scroll->widget()->findChildren<QToolButton*>())
        if(button->property("ribbonCommand").toBool())check(button->font().pixelSize()>=13,"Ribbon labels never shrink to unreadable 7 pixels");
    scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
    check(scroll->horizontalScrollBar()->value()==scroll->horizontalScrollBar()->maximum(),"Commands at ribbon end remain reachable by scrolling");
    scroll->horizontalScrollBar()->setValue(0);
    bool exampleOpened=false;
    QTimer::singleShot(0,&w,[&]{
        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        exampleOpened=dialog->objectName()=="examplesDialog" && dialog->findChild<QListWidget*>("exampleList")->count()>=12;
        dialog->findChild<QPushButton*>("gullstrandExampleButton")->click();
    });
    auto* examples=w.findChild<QToolButton*>("examplesButton");check(examples!=nullptr,"Visible ribbon command opens packaged examples");examples->click();
    check(exampleOpened && w.project().system.name.find("Гульстранда")!=std::string::npos,"Example picker opens the real Gullstrand project");
    const auto eye=w.project();
    check(eye.system.surfaces.size()==7 && eye.system.stop==2,"Gullstrand has six refracting surfaces and an explicit iris STOP");
    const auto para=paraxial(eye.system,eye.catalog,.5875618);
    near(1000*1.336/para.efl,58.64,.01,"Gullstrand power matches published rounded 58.64 D");
    near(eye.system.vertices().back()+para.bfl,24.387,.003,"Gullstrand focal plane agrees with published rounded position");
    check(w.results()->error.isEmpty() && w.results()->waveError.isEmpty() && w.results()->diffractionError.isEmpty(),"Gullstrand GUI computes spot, wavefront and PSF/MTF");
    check(!w.results()->geometryWarning.isEmpty(),"Trimmed nuclear continuations have a visible nonfatal diagnostic");
    auto* rayCount=w.findChild<QSpinBox*>("layoutRayCount");check(rayCount!=nullptr,"Sequential ray-count control is discoverable");
    const double rms=w.results()->spots[0].rms;rayCount->setValue(31);
    check(w.results()->layoutRays==31,"Ray-count control changes displayed beam sampling");
    near(w.results()->spots[0].rms,rms,1e-14,"Display ray count does not alter numerical analyses");
    QTemporaryDir temporary;const QString path=temporary.filePath("eye.optcad");
    auto selectFile=[&](const QString& file){QTimer::singleShot(0,&w,[&,file]{
        auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());if(!dialog)return;
        dialog->selectFile(file);dialog->findChild<QLineEdit*>("fileNameEdit")->setText(file);
        QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });};
    selectFile(path);w.findChild<QAction*>("saveAsAction")->trigger();
    check(QFile::exists(path) && loadProject(path).system.surfaces.size()==7,"Actual GUI saves Gullstrand to a native project");
    w.setProject(Project{});selectFile(path);w.findChild<QToolButton*>("openProjectButton")->click();
    check(w.project().system.name==eye.system.name,"Discoverable open command loads an existing example/project");
    w.setProject(Project{});
    const QString json=QString(OPTICS_SOURCE_DIR)+"/tests/data/geopter_singlet.json";
    QFile input(json);check(input.open(QIODevice::ReadOnly),"Geopter fixture opens");const auto before=input.readAll();input.close();
    selectFile(json);w.findChild<QAction*>("openAction")->trigger();
    check(w.project().system.name=="Synthetic interchange singlet","Ordinary Open also accepts supported Geopter JSON");
    const QString imported=temporary.filePath("imported.optcad");selectFile(imported);w.findChild<QAction*>("saveAction")->trigger();
    check(QFile::exists(imported),"Saving imported JSON asks for a new native path");
    check(input.open(QIODevice::ReadOnly),"Original Geopter JSON remains accessible");check(input.readAll()==before,"Saving an import never overwrites original Geopter JSON");input.close();
    w.setProject(eye);w.recalculate();
    auto wheel=[](PlotWidget& plot,QPointF at){QWheelEvent event(at,plot.mapToGlobal(at.toPoint()),{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(&plot,&event);plot.grab();};
    for(auto view : {View::Layout,View::Spot,View::Fan,View::OPD,View::Wavefront,View::PSF,View::MTF,View::RMS,View::Longitudinal,View::FieldCurvature,View::ChromaticFocus,View::GeometricMTF}) {
        PlotWidget plot(view);plot.resize(960,600);plot.setData(w.results());plot.show();plot.grab();
        check(!plot.axisViews().empty(),"Analysis exposes painted axes for interaction");
        auto first=plot.axisViews().front();wheel(plot,first.rect.center());auto both=plot.axisViews().front();
        check(both.xmax-both.xmin<first.xmax-first.xmin && both.ymax-both.ymin<first.ymax-first.ymin,"Wheel inside graph zooms both axes");
        wheel(plot,QPointF(both.rect.center().x(),both.rect.bottom()+12));auto x=plot.axisViews().front();
        check(x.xmax-x.xmin<both.xmax-both.xmin,"Wheel on X axis scales X");
        near(x.ymax-x.ymin,both.ymax-both.ymin,1e-9,"Wheel on X axis leaves Y unchanged");
        wheel(plot,QPointF(x.rect.left()-25,x.rect.center().y()));auto y=plot.axisViews().front();
        near(y.xmax-y.xmin,x.xmax-x.xmin,1e-9,"Wheel on Y axis leaves X unchanged");
        check(y.ymax-y.ymin<x.ymax-x.ymin,"Wheel on Y axis scales Y");
        const QPointF at(y.rect.left()+.25*y.rect.width(),y.rect.top()+.3*y.rect.height());
        const double underX=y.xmin+.25*(y.xmax-y.xmin),underY=y.ymin+.7*(y.ymax-y.ymin);
        wheel(plot,at);auto anchored=plot.axisViews().front();
        near(anchored.xmin+.25*(anchored.xmax-anchored.xmin),underX,1e-8,"Zoom preserves world X underneath cursor");
        near(anchored.ymin+.7*(anchored.ymax-anchored.ymin),underY,1e-8,"Zoom preserves world Y underneath cursor");
        plot.resetView();plot.grab();auto reset=plot.axisViews().front();
        near(reset.xmin,first.xmin,1e-9,"Reset restores original horizontal extent");near(reset.ymax,first.ymax,1e-9,"Reset restores original vertical extent");
        if(!dir.isEmpty())plot.grab().save(dir+"/feedback_view_"+QString::number(int(view))+".png");
    }
    auto sceneResults=std::make_shared<Results>();sceneResults->project=eye;
    sceneResults->scene=traceScene(eye.scene,eye.catalog);
    for(const auto& detector : sceneResults->scene->detectors) {
        const auto& object=eye.scene.objects[detector.objectIndex];
        sceneResults->detectorStats.push_back(detectorStatistics(detector,object.size.x,object.size.y));
    }
    for(auto view : {View::Detector,View::DetectorProfile}) {
        PlotWidget plot(view);plot.officePresentation=view==View::Detector;plot.resize(960,600);plot.setData(sceneResults);plot.show();plot.grab();
        check(!plot.axisViews().empty(),"Detector map/profile has zoomable physical coordinates");
        const auto before=plot.axisViews().front();wheel(plot,before.rect.center());const auto after=plot.axisViews().front();
        check(after.xmax-after.xmin<before.xmax-before.xmin && after.ymax-after.ymin<before.ymax-before.ymin,"Detector map/profile zoom affects both axes");
    }
    w.setProject(eye);QTest::qWait(120);
    if(!dir.isEmpty())w.grab().save(dir+"/gullstrand_window.png");
    auto invalid=eye;invalid.system.surfaces[0].material="MISSING_MATERIAL";w.setProject(invalid);w.recalculate();
    PlotWidget draft(View::Layout);draft.resize(900,600);draft.setData(w.results());draft.show();draft.grab();
    check(!w.results()->error.isEmpty() && !draft.axisViews().empty(),"An invalid draft keeps its geometric preview instead of a blank canvas");
    w.setProject(original);
    return checks;
}
