#include "optimization_editor.hpp"
#include "window.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <stdexcept>

size_t improvementChecks(Window& w, const QString& dir) {
    size_t checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) throw std::runtime_error(message);
    };
    auto click = [&](QString name) {
        auto* button = w.findChild<QAbstractButton*>(name);
        check(button, "New ribbon command is present");
        button->click();
    };
    Project project;
    optics::autofocus(project.system, project.catalog);
    auto plan = optics::defaultOptimization(project.system, project.catalog);
    plan.variables = {{optics::VariableParameter::Defocus, 0, -10, 10, .25}};
    plan.operands = {{optics::MeritKind::ImageDistance, -1, project.system.imageZ() + 1, 1, 1}};
    plan.minimumThroughput = 0;
    plan.iterations = 10;
    project.optimization = plan;
    auto restored = deserializeProject(serializeProject(project));
    check(restored.optimization && serializeProject(restored) == serializeProject(project),
          "Optimization variables, bounds and operands survive project roundtrip");
    Project legacy;
    auto old = deserializeProject(serializeProject(legacy));
    check(!old.optimization, "Older projects get no implicit optimization constraints");
    auto root = QJsonDocument::fromJson(serializeProject(project)).object();
    auto invalid = root;
    auto settings = invalid["optimization"].toObject();
    auto variables = settings["variables"].toArray();
    auto variable = variables[0].toObject();
    variable["parameter"] = 99; variables[0] = variable;
    settings["variables"] = variables; invalid["optimization"] = settings;
    bool rejected = false;
    try { deserializeProject(QJsonDocument(invalid).toJson()); } catch (...) { rejected = true; }
    check(rejected, "Project loader rejects unknown variable types");
    invalid = root;
    settings = invalid["optimization"].toObject(); settings["refocus"] = "yes";
    invalid["optimization"] = settings;
    rejected = false;
    try { deserializeProject(QJsonDocument(invalid).toJson()); } catch (...) { rejected = true; }
    check(rejected, "Project loader validates optimizer Boolean fields");
    auto reindexed = project;
    reindexed.optimization->variables = {{optics::VariableParameter::Radius, 0, 30, 80, 2},
                                         {optics::VariableParameter::Radius, 1, -80, -30, 2},
                                         {optics::VariableParameter::Defocus, 0, -10, 10, .2}};
    reindexed.system.surfaces.insert(reindexed.system.surfaces.begin() + 1, optics::Surface{});
    reindexOptimization(reindexed, 1, 0, 1);
    check(reindexed.optimization->variables[1].surface == 2 &&
              reindexed.optimization->variables[2].surface == 0,
          "Surface insertion preserves physical variable references and system defocus");
    reindexed.system.surfaces.erase(reindexed.system.surfaces.begin());
    reindexOptimization(reindexed, 0, 1, 0);
    check(reindexed.optimization->variables.size() == 2 &&
              reindexed.optimization->variables[0].surface == 1,
          "Deleting a surface removes its variables and reindexes the remaining references");
    w.setProject(project);
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    bool previewWorked = false, invalidStayedOpen = false, settingsSaved = false;
    QTimer::singleShot(0, &w, [&] {
        auto* dialog = dynamic_cast<OptimizationEditor*>(w.findChild<QDialog*>("optimizationDialog"));
        if (!dialog) return;
        auto* operands = dialog->findChild<QTableWidget*>("meritOperands");
        auto* vars = dialog->findChild<QTableWidget*>("optimizationVariables");
        dialog->findChild<QAbstractButton*>("evaluateMeritButton")->click();
        previewWorked = operands->item(0, 5)->text() != "—" &&
                        dialog->findChild<QLabel*>("meritPreviewMessage")->text().contains("безразмерный");
        vars->item(0, 4)->setText("-10");
        dialog->findChild<QAbstractButton*>("saveOptimizationButton")->click();
        invalidStayedOpen = dialog->isVisible() && dialog->result() != QDialog::Accepted;
        vars->item(0, 4)->setText("10");
        operands->item(0, 4)->setText("2");
        dialog->findChild<QAbstractButton*>("evaluateMeritButton")->click();
        if (!dir.isEmpty()) {
            dialog->grab().save(dir + "/optimization_variables.png");
            dialog->findChild<QTabWidget*>()->setCurrentIndex(1);
            dialog->grab().save(dir + "/optimization_editor.png");
        }
        dialog->findChild<QAbstractButton*>("saveOptimizationButton")->click();
        settingsSaved = dialog->result() == QDialog::Accepted;
        if (!settingsSaved) dialog->reject();
    });
    click("meritEditorButton");
    check(previewWorked, "Merit editor evaluates actual optics and fills individual values");
    check(invalidStayedOpen, "Merit editor rejects invalid parameter bounds without accepting the dialog");
    check(settingsSaved && w.project().optimization->operands[0].weight == 2,
          "Merit editor saves user-defined weights into the project");
    w.findChild<QAction*>("undoAction")->trigger();
    check(w.project().optimization->operands[0].weight == 1,
          "Undo restores optimization settings independently of geometry");
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    auto before = serializeProject(w.project());
    click("customOptimizeButton");
    check(w.optimizing(), "Optimizer stays busy until its result is delivered to the UI");
    click("customOptimizeButton");
    w.runScene();
    check(w.optimizing() && !w.calculating(), "Repeated launches and simultaneous scene tracing are blocked");
    check(QTest::qWaitFor([&] { return !w.optimizing(); }, 10000), "Configured optimization finishes asynchronously");
    check(std::abs(w.project().system.defocus - 1) < 1e-12,
          "Configured optimization changes the selected parameter to its analytical target");
    auto* report = w.findChild<QDialog*>("optimizationReportDialog");
    check(report && report->findChild<QPlainTextEdit*>("optimizationReportText")->toPlainText().contains("Критерии:"),
          "Optimization report includes before/after operand values");
    if (!dir.isEmpty() && report) report->grab().save(dir + "/optimization_report.png");
    if (report) report->close();
    QApplication::processEvents();
    w.findChild<QAction*>("undoAction")->trigger();
    check(serializeProject(w.project()) == before, "Undo restores the full pre-optimization project");
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    click("customOptimizeButton");
    click("cancelOptimizationButton");
    check(QTest::qWaitFor([&] { return !w.optimizing(); }, 10000), "Optimizer cancellation reaches the GUI completion state");
    check(serializeProject(w.project()) == before, "Cancelling from the ribbon commits no geometry or settings");
    click("customOptimizeButton");
    w.findChild<QTableWidget*>("surfaceTable")->item(1, 3)->setText("60");
    check(QTest::qWaitFor([&] { return !w.optimizing(); }, 10000), "Optimizer completes after concurrent editing");
    check(w.project().system.surfaces[0].radius == 60 && w.project().system.defocus == 0,
          "Stale optimization results never overwrite newer user edits");
    w.setProject(project);
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(0);
    w.findChild<QAbstractButton*>("addSurfaceButton")->click();
    check(w.project().optimization && w.project().optimization->variables[0].parameter == optics::VariableParameter::Defocus,
          "Existing surface insertion remains usable with optimization settings");
    w.findChild<QAction*>("undoAction")->trigger();
    check(w.project().system.surfaces.size() == 2 && w.project().optimization.has_value(),
          "Undo restores inserted geometry and its optimizer references together");
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
    click("optimizeButton");
    check(QTest::qWaitFor([&] { return !w.optimizing(); }, 10000) &&
              w.project().system.validate(w.project().catalog).empty(),
          "Original radius optimization still runs through the GUI");
    report = w.findChild<QDialog*>("optimizationReportDialog");
    if (report) report->close();
    QApplication::processEvents();
    {
        auto* closing = new Window;
        closing->setProject(project);
        closing->findChild<QTabBar*>("modeTabs")->setCurrentIndex(3);
        closing->findChild<QAbstractButton*>("customOptimizeButton")->click();
        check(closing->optimizing(), "Closing scenario begins with a running optimizer");
        delete closing;
        QApplication::processEvents();
        check(true, "Closing a window safely waits for its optimizer worker");
    }
    project.mode = 1;
    auto extra = project.scene.objects.back();
    extra.pose.position.z = 200;
    extra.name = "Второй детектор";
    project.scene.objects.push_back(extra);
    w.setProject(project);
    click("traceSceneButton");
    check(QTest::qWaitFor([&] { return !w.calculating(); }, 15000) && w.results()->detectorStats.size() == 2,
          "A real scene trace computes statistics for every detector");
    const auto& first = w.results()->detectorStats[0];
    check(first.hasPower && first.power == w.results()->scene->detectors[0].totalPower(),
          "Selected detector statistics preserve actual traced power");
    w.findChild<QTabBar*>("modeTabs")->setCurrentIndex(2);
    click("analysis_14");
    auto* tabs = w.findChild<QTabWidget*>("analysisTabs");
    auto* plot = dynamic_cast<PlotWidget*>(tabs->currentWidget());
    check(plot && plot->view == View::DetectorProfile && !plot->grab().isNull(),
          "Detector profiles render in an analysis tab");
    if (!dir.isEmpty()) {
        plot->grab().save(dir + "/detector_profiles.png");
        w.grab().save(dir + "/detector_analysis.png");
    }
    QTemporaryDir temporary;
    auto selectFile = [&](QString path) {
        QTimer::singleShot(0, &w, [&, path] {
            if (auto* dialog = w.findChild<QFileDialog*>()) {
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
    };
    const auto profilePath = temporary.path() + "/profiles.csv";
    selectFile(profilePath); click("csvButton");
    QFile profiles(profilePath);
    check(profiles.open(QIODevice::ReadOnly), "Active detector profile exports to a real CSV file");
    auto bytes = profiles.readAll();
    check(bytes.startsWith("axis,position_mm,integrated_power_W_per_mm\n") && bytes.contains("\nY,"),
          "Profile CSV identifies both axes and their physical units");
    const auto statisticsPath = temporary.path() + "/statistics.csv";
    selectFile(statisticsPath); click("detectorStatisticsCSVButton");
    QFile statistics(statisticsPath);
    check(statistics.open(QIODevice::ReadOnly) && statistics.readAll().contains("radius_80,"),
          "Detector summary command exports quantitative footprint metrics");
    auto* selector = w.findChild<QComboBox*>("detectorCombo");
    selector->setCurrentIndex(1);
    check(w.results()->detector == 1 && !w.results()->detectorStats[1].hasPower,
          "Selecting an unilluminated detector updates profiles without inventing moments");
    check(detectorStatisticsCSV(w.results()->scene->detectors[1], w.results()->detectorStats[1], 1, true)
              .contains("centroid_x,,mm\n"),
          "Undefined zero-power detector moments are blank in CSV");
    check(detectorStatisticsCSV(w.results()->scene->detectors[0], first, 1, true)
              .contains("partial_trace,1,boolean\n"),
          "Partial trace status is retained in detector summaries");
    w.findChild<QTableWidget*>("objectTable")->item(0, 6)->setText("30");
    check(!w.results()->scene && w.results()->detectorStats.empty(),
          "Editing scene geometry invalidates both maps and derived detector metrics");
    w.setProject(Project{});
    return checks;
}
