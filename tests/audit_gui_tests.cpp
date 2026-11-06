#include "automation.hpp"
#include "plot_scale.hpp"
#include "ready_results.hpp"
#include "shape_editor.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPushButton>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>

using namespace optics;

size_t auditGUIChecks(Window& w) {
    size_t count = 0;
    auto check = [&](bool condition, const char* why) {
        ++count;
        if (!condition)
            throw std::runtime_error(why);
    };
    auto analyze = [](const Project& p) {
        const auto input = QJsonDocument::fromJson(serializeProject(p)).object();
        return executeRequest({{"project", input},
                               {"operation", "analyze"},
                               {"options", QJsonObject{{"pupil_grid", 9}}}})["fields"]
            .toArray()[0]
            .toObject();
    };
    auto xy = freeformProject();
    xy.system.fields = {{std::atan(.01) / deg, 0, 1}};
    xy.system.surfaces[0].xySag = {};
    const auto flat = analyze(xy);
    xy.system.surfaces[0].xySag[0] = 1e-18;
    const auto curved = analyze(xy);
    check(flat["survived"].toInt() == 147 && curved["survived"].toInt() == 147 &&
              std::abs(flat["rms_mm"].toDouble() - curved["rms_mm"].toDouble()) < 1e-10,
          "Nearly planar shipped freeform retains the same 147 rays and spot radius");
    Project p;
    p.system.surfaces.front().transmission = 0;
    const auto dark = analyze(p);
    check(dark["spot_status"] == "no_power" && dark["rms_mm"].isNull() &&
              dark["centroid_mm"].isNull(),
          "Zero-energy spot is explicitly undefined in batch results");

    p = Project{};
    p.system.surfaces.front().radius = 1e8;
    p.system.surfaces.front().semiDiameter = 8.123456789012345;
    ShapeEditor shape(p, 0);
    shape.findChild<QPushButton*>("shapeSave")->click();
    check(shape.result() == QDialog::Accepted && shape.surface().radius == 1e8 &&
              shape.surface().semiDiameter == p.system.surfaces.front().semiDiameter,
          "Accepting the profile preserves imported radius and full unedited precision");

    p = Project{};
    Material ir;
    ir.name = "IR_ONLY";
    ir.nd = 1.5;
    ir.minWavelength = 1;
    ir.maxWavelength = 2;
    p.catalog.add(ir);
    w.setProject(p);
    bool shown = false;
    QTimer::singleShot(0, &w, [&] {
        auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!d)
            return;
        auto* table = d->findChild<QTableWidget*>();
        if (table)
            for (int row = 0; row < table->rowCount(); ++row)
                if (table->item(row, 0)->text() == "IR_ONLY")
                    shown = table->item(row, 1)->text() == "—" &&
                            table->item(row, 3)->text() == "—" &&
                            table->item(row, 4)->text() == "—";
        d->reject();
    });
    for (auto* button : w.findChildren<QToolButton*>())
        if (button->text().contains("Каталог")) {
            button->click();
            break;
        }
    check(shown, "IR-only catalog row remains usable without visible-line indices");

    p.system.fields = {{0, 0, 1}};
    p.optimization = defaultOptimization(p.system, p.catalog);
    std::erase_if(p.optimization->variables,
                  [](auto v) { return v.parameter == VariableParameter::Defocus; });
    p.optimization->refocus = p.optimization->allFieldsFocus = true;
    w.setProject(p);
    QTimer::singleShot(0, &w, [&] {
        auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!d)
            return;
        d->findChild<QComboBox*>("parameterFieldType")
            ->setCurrentIndex(int(FieldType::RealImageHeight));
        QTimer::singleShot(0, d, [] {
            if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                m->accept();
        });
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        d->reject();
    });
    w.findChild<QToolButton*>("parametersButton")->click();
    check(w.project().system.fieldType == FieldType::Angle,
          "Invalid field/optimization combination cannot be applied");
    QTemporaryDir tmp;
    const auto path = tmp.filePath("design.optcad");
    saveProject(path, p);
    auto invalid = p;
    invalid.system.fieldType = FieldType::RealImageHeight;
    bool rejected = false;
    try {
        saveProject(path, invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected && serializeProject(loadProject(path)) == serializeProject(p),
          "Failed save preserves last readable project");

    p = Project{};
    p.system.surfaces[0].aperture = {ApertureShape::Ellipse, 2, 1, 0};
    for (bool spectral : {false, true}) {
        p.workspace["polychromaticDiffraction"] = spectral;
        w.setProject(p);
        auto result = readyResults(w);
        check(result->diffraction.psf.empty() && !result->diffractionError.isEmpty(),
              "Both PSF modes reject an uncertified ellipse");
    }
    const auto ticks = plotScale::ticks(-3.7, 8.1, 460, 72);
    check(ticks == std::vector<double>{-2, 0, 2, 4, 6, 8},
          "Ticks stay anchored to zero with a readable step");
    auto* modes = w.findChild<QTabBar*>("modeTabs");
    p = Project{};
    p.scene.rayCount = 1200;
    w.setProject(p);
    for (int iteration = 0; iteration < 12; ++iteration) {
        modes->setCurrentIndex(1);
        w.runScene();
        if (iteration % 2 == 0)
            check(QTest::qWaitFor([&] { return !w.calculating(); }, 15000),
                  "Scene completes before switching");
        modes->setCurrentIndex(2);
        w.findChild<QToolButton*>("analysis_14")->click();
        modes->setCurrentIndex(0);
        check(QTest::qWaitFor([&] { return !w.calculating() && !w.analyzing(); }, 30000),
              "Mode transition settles both workers");
        check(w.project().mode == 0 && !w.results()->spots.empty() && w.results()->error.isEmpty(),
              "Detector analysis switches safely to current sequential results");
    }

    p = Project{};
    p.system.fields.resize(50, Field{0, 3.5, 1});
    p.system.wavelengths.resize(20, Wavelength{.5875618, 1});
    p.system.pupilGrid = 33;
    int ticksWhileBusy = 0;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &w, [&] {
        if (w.analyzing())
            ++ticksWhileBusy;
    });
    QElapsedTimer elapsed;
    elapsed.start();
    timer.start();
    w.setProject(p);
    auto large = readyResults(w);
    timer.stop();
    qInfo("50 fields, 20 wavelengths, grid 33: %lld ms including UI, %d timer events",
          elapsed.elapsed(), ticksWhileBusy);
    check(ticksWhileBusy > 0 && large->spots.size() == 50,
          "Large sequential job finishes while GUI continues servicing timers");
    w.setProject(p);
    w.setProject(Project{});
    check(readyResults(w)->spots.size() == Project{}.system.fields.size(),
          "Superseded large job cannot overwrite the new model");
    return count;
}
