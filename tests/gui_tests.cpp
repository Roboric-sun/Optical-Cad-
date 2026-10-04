#include "window.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>
#include <stdexcept>

static int checks = 0;
size_t improvementChecks(Window&, const QString&);
size_t comboChecks(Window&, const QString&);
size_t solveGUIChecks(Window&, const QString&);
size_t marginalGUIChecks(Window&, const QString&);
static void check(bool b, const char* text) {
    ++checks;
    if (!b)
        throw std::runtime_error(text);
}
static void click(Window& w, const QString& name) {
    auto* b = w.findChild<QAbstractButton*>(name);
    check(b != nullptr, "Command exists");
    b->click();
    QApplication::processEvents();
}
static bool paintedData(const QPixmap& pixmap) {
    QImage image = pixmap.toImage();
    int count = 0;
    for (int y = 80; y < image.height() - 30; y += 2)
        for (int x = 60; x < image.width() - 30; x += 2) {
            QColor c = image.pixelColor(x, y);
            if (c.saturation() > 115 && c.value() > 85 && ++count > 20)
                return true;
        }
    return false;
}
int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        check(!QFontDatabase::families().isEmpty(),
              "System fonts available for GUI layout and text rendering");
        QTemporaryDir temp;
        Project original;
        optics::autofocus(original.system, original.catalog);
        saveProject(temp.path() + "/roundtrip.optcad", original);
        auto restored = loadProject(temp.path() + "/roundtrip.optcad");
        check(serializeProject(restored) == serializeProject(original),
              "Project roundtrip preserves every parameter");
        auto corrupted = serializeProject(original);
        corrupted.replace("\"version\": 1", "\"version\": 99");
        bool rejected = false;
        try {
            deserializeProject(corrupted);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Unsupported file version rejected");
        for (const QString name : {"singlet", "achromat", "led_illuminator"}) {
            auto legacy = loadProject(QString(OPTICS_SOURCE_DIR) + "/examples/" + name + ".optcad");
            check(legacy.system.validate(legacy.catalog).empty() &&
                      legacy.scene.validate(legacy.catalog).empty(),
                  "Existing v0.2 examples remain readable and physically valid");
        }
        auto prismProject = original;
        prismProject.mode = 1;
        prismProject.scene = optics::Scene::prismDemo(prismProject.catalog);
        auto prismRestored = deserializeProject(serializeProject(prismProject));
        check(prismRestored.scene.objects[0].kind == optics::ObjectKind::Prism &&
                  serializeProject(prismRestored) == serializeProject(prismProject),
              "Prism geometry, pose and spectral sources survive a complete file roundtrip");
        QFile fixture(QString(OPTICS_SOURCE_DIR) + "/tests/data/geopter_singlet.json");
        check(fixture.open(QIODevice::ReadOnly), "Geopter fixture readable");
        auto fixtureBytes = fixture.readAll();
        auto imported = importGeopter(fixtureBytes);
        check(imported.system.surfaces.size() == 2 && imported.system.stop == 0,
              "Geopter object/image indexing converted");
        check(std::abs(imported.system.wavelengths[0].um - .5875618) < 1e-12,
              "Geopter nm to um conversion");
        check(imported.system.surfaces[0].radius == 50 &&
                  imported.system.surfaces[0].material == "N-BK7",
              "Curvature and supplier material conversion");
        check(std::abs(optics::paraxial(imported.system, imported.catalog, .5875618).efl -
                       49.2129958) < 1e-6,
              "Imported singlet analytical EFL");
        fixtureBytes.replace("\"Type\": \"SPH\"", "\"Type\": \"ODD\"");
        rejected = false;
        try {
            importGeopter(fixtureBytes);
        } catch (...) {
            rejected = true;
        }
        check(rejected, "Unsupported Geopter surface rejected");
        Window w;
        w.show();
        QTest::qWait(150);
        check(!w.results()->spots.empty(), "Live sequential analysis");
        check(w.results()->error.isEmpty() && w.results()->waveError.isEmpty(),
              "Demo analyses succeed");
        auto* rs = w.findChild<QScrollArea*>("ribbonScroll");
        const bool sequentialFits = QTest::qWaitFor(
            [&] { return rs->horizontalScrollBar()->maximum() == 0; }, 1500);
        if (!sequentialFits) {
            auto* ribbon = rs->widget();
            std::cerr << "Ribbon: window=" << w.width() << " viewport="
                      << rs->viewport()->width() << " contents=" << ribbon->width()
                      << " minimum=" << ribbon->minimumSizeHint().width() << " natural="
                      << ribbon->property("naturalWidth").toInt() << " steps="
                      << ribbon->property("compressionSteps").toInt() << " overflow="
                      << rs->horizontalScrollBar()->maximum() << '\n';
            for (auto* button : ribbon->findChildren<QToolButton*>())
                std::cerr << button->objectName().toStdString() << " font="
                          << button->font().pixelSize() << " hint=" << button->sizeHint().width()
                          << " width=" << button->width() << '\n';
        }
        check(sequentialFits, "Sequential ribbon fits default window");
        auto* modes = w.findChild<QTabBar*>("modeTabs");
        check(modes->count() == 9 && modes->tabText(5) == "Библиотеки",
              "Reference ribbon context tabs");
        auto* lensButton = w.findChild<QToolButton*>("addLensButton");
        check(lensButton && !lensButton->icon().isNull() &&
                  lensButton->toolButtonStyle() == Qt::ToolButtonTextUnderIcon,
              "Vector ribbon artwork with large commands");
        auto* tree = w.findChild<QTreeWidget*>("systemTree");
        auto* search = w.findChild<QLineEdit*>("treeSearch");
        search->setText("no_matching_node_1234");
        check(tree->topLevelItem(0)->isHidden(), "Navigator search hides unrelated branches");
        search->clear();
        check(!tree->topLevelItem(0)->isHidden(), "Clearing navigator search restores model");
        auto* firstSurface = tree->topLevelItem(0)->child(1)->child(1);
        const double initialRMS = w.results()->spots[0].rms;
        QPoint eye(tree->header()->sectionViewportPosition(2) + tree->columnWidth(2) / 2,
                   tree->visualItemRect(firstSurface).center().y());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, eye);
        check(w.results()->hidden.contains(0) && w.results()->spots[0].rms == initialRMS,
              "Eye control changes presentation without changing ray physics");
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, eye);
        check(!w.results()->hidden.contains(0), "Eye control restores visible geometry");
        auto* table = w.findChild<QTableWidget*>("surfaceTable");
        check(table && table->rowCount() == 4, "Surface editor populated");
        double r = w.project().system.surfaces[0].radius;
        table->item(1, 3)->setText("60");
        QTest::qWait(300);
        check(w.project().system.surfaces[0].radius == 60, "Cell edit changes optical model");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(200);
        check(w.project().system.surfaces[0].radius == r, "Undo restores physical model");
        auto* material = qobject_cast<QComboBox*>(table->cellWidget(1, 5));
        check(material != nullptr, "Glass selector present");
        material->setCurrentText("F2");
        QTest::qWait(200);
        check(w.project().system.surfaces[0].material == "F2",
              "Glass selector changes dispersive model");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(150);
        check(table->isColumnHidden(8) && table->isColumnHidden(15) && !table->isColumnHidden(14) &&
                  !table->isColumnHidden(11),
              "Compact surface columns match the reference grouping");
        auto setProperty = [&](int column, int component, double value) {
            bool found = false;
            QTimer::singleShot(0, &w, [&] {
                auto* dialog = w.findChild<QDialog*>("surfacePropertyDialog");
                if (dialog) {
                    auto* spin = dialog->findChild<QDoubleSpinBox*>(
                        QString("surfaceProperty_%1").arg(component));
                    found = spin != nullptr;
                    if (spin)
                        spin->setValue(value);
                    dialog->accept();
                }
            });
            QMetaObject::invokeMethod(table, "cellDoubleClicked", Qt::DirectConnection,
                                      Q_ARG(int, 1), Q_ARG(int, column));
            QApplication::processEvents();
            return found;
        };
        check(setProperty(14, 0, .8) && w.project().system.surfaces[0].transmission == .8,
              "Compact coating dialog edits the actual optical coefficient");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(150);
        check(w.project().system.surfaces[0].transmission == 1,
              "Undo restores a compound coating edit");
        check(setProperty(11, 3, .25) && w.project().system.surfaces[0].decenter.x == .25,
              "Compact orientation dialog preserves editable decenter coordinates");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(150);
        check(w.project().system.surfaces[0].decenter.x == 0, "Undo restores a compound pose edit");
        auto* workspace = w.findChild<QTabWidget*>("analysisTabs");
        for (int i = 0; i < workspace->count(); ++i)
            if (auto* p = dynamic_cast<PlotWidget*>(workspace->widget(i));
                p && p->view == View::Layout)
                workspace->setCurrentIndex(i);
        click(w, "addSurfaceButton");
        check(w.project().system.surfaces.size() == 3, "Surface insertion");
        click(w, "deleteSurfaceButton");
        check(w.project().system.surfaces.size() == 2, "Surface removal");
        check(table->cellWidget(3, 2) == nullptr && table->cellWidget(3, 5) == nullptr &&
                  !(table->item(3, 2)->flags() & Qt::ItemIsEditable),
              "Image endpoint stays read-only after removing a surface");
        click(w, "autofocusButton");
        const auto args = app.arguments();
        int shot = args.indexOf("--screenshots");
        QString dir;
        if (shot >= 0 && shot + 1 < args.size()) {
            dir = args[shot + 1];
            QDir().mkpath(dir);
            w.grab().save(dir + "/sequential.png");
        }
        for (int id : {1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13}) {
            if (id >= 10)
                w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(2);
            click(w, "analysis_" + QString::number(id));
            auto* tabs = w.findChild<QTabWidget*>("analysisTabs");
            check(tabs->currentWidget() != nullptr, "Analysis view opens");
            auto pixmap = tabs->currentWidget()->grab();
            check(!pixmap.isNull(), "Analysis view renders");
            check(paintedData(pixmap), "Analysis draws calculated data beyond its toolbar");
            if (id == 2) {
                auto& fan = w.results()->fan;
                check(w.results()->fanError.isEmpty() && fan.tangential.y.size() == 3 &&
                          fan.sagittal.y.size() == 3,
                      "Live ray-fan view receives both physical pupil sections");
                auto csvPath = temp.path() + "/ray_fans.csv";
                QTimer::singleShot(0, &w, [&] {
                    auto* dialog = w.findChild<QFileDialog*>();
                    if (dialog) {
                        dialog->selectFile(csvPath);
                        QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                    }
                });
                click(w, "csvButton");
                QFile csv(csvPath);
                check(csv.open(QIODevice::ReadOnly), "Ribbon exports ray-fan CSV to a real file");
                auto contents = csv.readAll();
                check(contents.startsWith("wavelength_um,pupil,tangential_um,sagittal_um,") &&
                          contents.count('\n') == 184,
                      "Fan CSV carries physical units and both sections for all 183 rays");
                auto clipped = original.system;
                clipped.surfaces[1].semiDiameter = .1;
                auto clippedCSV = rayFanCSV(optics::rayFan(clipped, original.catalog, {0, 0, 1}));
                check(clippedCSV.contains(",,,0,0\n") && !clippedCSV.contains("nan"),
                      "CSV records vignetted samples as missing data with validity flags");
                if (!dir.isEmpty())
                    pixmap.save(dir + "/ray_fans.png");
            }
            if (!dir.isEmpty())
                pixmap.save(dir + "/analysis_" + QString::number(id) + ".png");
        }
        w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(1);
        QTest::qWait(100);
        check(w.project().mode == 1, "Nonsequential mode switches");
        check(QTest::qWaitFor([&] { return rs->horizontalScrollBar()->maximum() == 0; }, 1500),
              "Nonsequential ribbon fits default window");
        check(w.findChild<QTableWidget*>("objectTable")->rowCount() == 2,
              "Object editor populated");
        click(w, "traceSceneButton");
        QElapsedTimer time;
        time.start();
        while (w.calculating() && time.elapsed() < 15000)
            QTest::qWait(25);
        QApplication::processEvents();
        check(!w.calculating() && w.results()->scene.has_value(),
              "Async nonsequential trace completes");
        check(w.results()->scene->detectedPower > 0, "Detector records physical power");
        auto* tabs = w.findChild<QTabWidget*>("analysisTabs");
        auto* scenePlot = dynamic_cast<PlotWidget*>(tabs->currentWidget());
        check(scenePlot && scenePlot->view == View::Scene,
              "Trace keeps 3D scene active above detector");
        check(w.findChild<QDockWidget*>("detectorDock")->isVisible(),
              "Detector panel is simultaneously visible");
        QTest::mouseClick(scenePlot, Qt::LeftButton, Qt::NoModifier,
                          QPoint(scenePlot->width() - 60, 63));
        check(scenePlot->pitch == 90, "Orientation cube selects exact top view");
        scenePlot->resetView();
        auto* display = w.findChild<QSpinBox*>("displayRayCount");
        display->setValue(12);
        check(w.results()->displayRays == 12 && w.results()->scene->launched == 10000,
              "Display ray count is independent of statistical ray count");
        display->setValue(80);
        click(w, "objectEditorButton");
        check(tabs->currentWidget()->objectName() == "editorWorkspace" &&
                  w.findChild<QTableWidget*>("objectTable")->isVisible(),
              "Editor command opens working scene table");
        tabs->setCurrentWidget(scenePlot);
        check(paintedData(scenePlot->grab()), "3D view draws physical geometry and ray segments");
        if (!dir.isEmpty()) {
            w.grab().save(dir + "/nonsequential.png");
            auto* tabs = w.findChild<QTabWidget*>("analysisTabs");
            for (int i = 0; i < tabs->count(); ++i)
                if (auto* p = dynamic_cast<PlotWidget*>(tabs->widget(i));
                    p && p->view == View::Scene) {
                    tabs->setCurrentIndex(i);
                    w.grab().save(dir + "/scene.png");
                }
            w.resize(1920, 1080);
            w.resizeDocks({w.findChild<QDockWidget*>("navigatorDock")}, {365}, Qt::Horizontal);
            w.resizeDocks({w.findChild<QDockWidget*>("detectorDock")}, {320}, Qt::Vertical);
            QTest::qWait(100);
            w.grab().save(dir + "/reference_nonsequential.png");
            w.resize(1280, 800);
            w.resizeDocks({w.findChild<QDockWidget*>("navigatorDock")}, {245}, Qt::Horizontal);
            w.resizeDocks({w.findChild<QDockWidget*>("detectorDock")}, {245}, Qt::Vertical);
            QTest::qWait(100);
            w.grab().save(dir + "/compact_nonsequential.png");
        }
        w.findChild<QTableWidget*>("objectTable")->item(0, 6)->setText("30");
        QTest::qWait(250);
        check(!w.results()->scene.has_value(), "Editing invalidates stale detector map");
        click(w, "object_6");
        check(w.project().scene.objects.back().kind == optics::ObjectKind::Prism,
              "Previously disabled prism ribbon command creates a real dielectric object");
        auto* objectTable = w.findChild<QTableWidget*>("objectTable");
        auto* prismType = qobject_cast<QComboBox*>(objectTable->cellWidget(2, 2));
        check(prismType && prismType->currentText() == "Призма",
              "Prism type is available in the working scene editor");
        objectTable->item(2, 12)->setText("20");
        QTest::qWait(250);
        check(w.project().scene.objects.back().size.z == 20,
              "Prism dimensions are edited through the existing object table");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(150);
        check(std::abs(w.project().scene.objects.back().size.z - 10 * sqrt(3.)) < 1e-12,
              "Undo restores prism geometry");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(150);
        check(w.project().scene.objects.size() == 2, "Undo removes a newly added prism");
        w.setProject(prismProject);
        click(w, "traceSceneButton");
        time.restart();
        while (w.calculating() && time.elapsed() < 15000)
            QTest::qWait(25);
        check(!w.calculating() && w.results()->scene && w.results()->error.isEmpty() &&
                  w.results()->scene->launched == 30000 && w.results()->scene->detectedPower > .8,
              "Three-line prism example traces from the GUI and reaches its detector");
        auto* prismPlot =
            dynamic_cast<PlotWidget*>(w.findChild<QTabWidget*>("analysisTabs")->currentWidget());
        check(prismPlot && paintedData(prismPlot->grab()),
              "Prism faces and calculated ray paths render in the 3D scene");
        auto pathCSV = temp.path() + "/prism_paths.csv";
        QTimer::singleShot(0, &w, [&] {
            if (auto* dialog = w.findChild<QFileDialog*>()) {
                dialog->selectFile(pathCSV);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        click(w, "csvButton");
        QFile rayFile(pathCSV);
        check(rayFile.open(QIODevice::ReadOnly), "Prism ray paths export from the GUI");
        auto rayCSV = rayFile.readAll();
        check(rayCSV.startsWith("ray,segment,x_mm,y_mm,z_mm,source_index,wavelength_um,") &&
                  rayCSV.contains(",0.4861327,") && rayCSV.contains(",0.5875618,") &&
                  rayCSV.contains(",0.6562725,"),
              "Ray-path CSV identifies every source, wavelength and interaction category");
        if (!dir.isEmpty())
            w.grab().save(dir + "/prism_scene.png");
        checks += int(improvementChecks(w, dir));
        w.setProject(original);
        checks += int(comboChecks(w, dir));
        checks += int(solveGUIChecks(w, dir));
        checks += int(marginalGUIChecks(w, dir));
        w.hide();
        std::cout << checks << " GUI and persistence checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
