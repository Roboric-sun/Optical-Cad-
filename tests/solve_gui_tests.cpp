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
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <stdexcept>

using namespace optics;
size_t solveGUIChecks(Window& w, const QString& dir) {
    size_t checks = 0;
    auto check = [&](bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); };
    auto close = [&](double a, double b, const char* message) { check(std::abs(a - b) < 1e-9, message); };
    const auto original = w.project();
    w.setProject(Project{});
    auto* table = w.findChild<QTableWidget*>("surfaceTable");
    auto edit = [&](int row, int column, std::function<void(SolveEditor*)> configure) {
        bool menuFound = false, dialogFound = false;
        table->setCurrentCell(row + 1, column);
        QTimer::singleShot(0, &w, [&] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto* action : menu->actions())
                if (action->text() == (column == 3 ? "Связь радиуса…" : "Расчёт толщины…")) {
                    menuFound = true;
                    QTimer::singleShot(0, &w, [&] {
                        auto* dialog = qobject_cast<SolveEditor*>(QApplication::activeModalWidget());
                        if (!dialog) return;
                        dialogFound = true;
                        configure(dialog);
                    });
                    QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(action).center());
                    return;
                }
            menu->close();
        });
        QMetaObject::invokeMethod(table, "customContextMenuRequested", Qt::DirectConnection,
                                  Q_ARG(QPoint, table->visualItemRect(table->item(row + 1, column)).center()));
        check(menuFound && dialogFound, "Surface context menu opens its parameter solve editor");
        QTest::qWait(150);
    };
    auto accept = [](SolveEditor* dialog) {
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    };
    edit(1, 3, [&](SolveEditor* d) {
        d->findChild<QComboBox*>("solveType")->setCurrentIndex(1);
        d->findChild<QComboBox*>("solveReference")->setCurrentIndex(0);
        d->findChild<QDoubleSpinBox*>("solveScale")->setValue(-1);
        if (!dir.isEmpty()) d->grab().save(dir + "/radius_pickup.png");
        accept(d);
    });
    check(w.project().system.solves.size() == 1, "Radius pickup committed from the UI");
    check(!(table->item(2,3)->flags() & Qt::ItemIsEditable) && !table->item(2,3)->icon().isNull(), "Dependent radius has a visible marker and cannot be edited directly");
    table->item(1,3)->setText("65");
    QTest::qWait(200);
    close(w.project().system.surfaces[1].radius, -65, "Changing the source updates its pickup immediately");
    close(table->item(2,3)->text().toDouble(), -65, "Dependent cell displays the newly calculated radius");
    check(w.results()->error.isEmpty(), "Updated pickup geometry recalculates successfully");
    w.findChild<QAction*>("undoAction")->trigger();
    close(w.project().system.surfaces[1].radius, -50, "Undo source edit updates its dependent value");
    w.findChild<QAction*>("undoAction")->trigger();
    check(w.project().system.solves.empty() && (table->item(2,3)->flags() & Qt::ItemIsEditable), "Undo removes the constraint and restores manual editing");
    w.findChild<QAction*>("redoAction")->trigger();
    check(w.project().system.solves.size() == 1, "Redo restores the constraint metadata");
    edit(0, 4, [&](SolveEditor* d) {
        d->findChild<QComboBox*>("solveType")->setCurrentIndex(2);
        d->findChild<QDoubleSpinBox*>("solveValue")->setValue(1);
        d->findChild<QDoubleSpinBox*>("solveHeight")->setValue(12.5);
        if (!dir.isEmpty()) d->grab().save(dir + "/edge_thickness.png");
        accept(d);
    });
    check(w.project().system.solves.size() == 2, "Edge thickness committed alongside radius pickup");
    close(w.project().system.surfaces[0].thickness, 1 + 2 * (50 - std::sqrt(2500 - 12.5 * 12.5)), "GUI edge thickness matches an independent spherical calculation");
    const auto bytes = serializeProject(w.project());
    check(QJsonDocument::fromJson(bytes).object()["version"] == 2, "Constrained projects use a format older readers reject");
    check(serializeProject(deserializeProject(bytes)) == bytes, "Constraints and resolved values survive a full roundtrip");
    auto bad = QJsonDocument::fromJson(bytes).object();
    auto sequential = bad["sequential"].toObject();
    auto rules = sequential["solves"].toArray();
    auto rule = rules[0].toObject(); rule["kind"] = 99; rules[0] = rule;
    sequential["solves"] = rules; bad["sequential"] = sequential;
    bool rejected = false;
    try { deserializeProject(QJsonDocument(bad).toJson()); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Unknown serialized solve type rejected");

    SolveEditor cycle(w.project(), 0, SolveParameter::Radius, &w);
    cycle.show();
    cycle.findChild<QComboBox*>("solveType")->setCurrentIndex(1);
    cycle.findChild<QComboBox*>("solveReference")->setCurrentIndex(1);
    accept(&cycle);
    check(cycle.result() != QDialog::Accepted && cycle.findChild<QLabel*>("solveMessage")->text().contains("Цикл"), "Solve editor reports a cycle without closing or changing the model");
    cycle.reject();
    Project optimized;
    optimized.optimization = defaultOptimization(optimized.system, optimized.catalog);
    SolveEditor conflict(optimized, 1, SolveParameter::Radius, &w);
    conflict.findChild<QComboBox*>("solveType")->setCurrentIndex(1);
    conflict.findChild<QComboBox*>("solveReference")->setCurrentIndex(0);
    conflict.findChild<QDoubleSpinBox*>("solveScale")->setValue(-1);
    accept(&conflict);
    check(conflict.result() != QDialog::Accepted && conflict.findChild<QLabel*>("solveMessage")->text().contains("переменной"), "Editor prevents conflicts with a saved optimization plan");
    const auto constrained = w.project();
    edit(1, 3, [&](SolveEditor* d) { d->findChild<QComboBox*>("solveType")->setCurrentIndex(0); d->reject(); });
    check(serializeProject(w.project()) == bytes, "Cancel leaves the constraints and all geometry intact");
    auto* surfaceType = qobject_cast<QComboBox*>(table->cellWidget(1,2));
    surfaceType->setCurrentIndex(1);
    QTest::qWait(200);
    check(!w.results()->error.isEmpty() && w.results()->spots.empty(), "Incompatible surface kind reports an error instead of showing stale calculations");
    w.findChild<QAction*>("undoAction")->trigger();
    QTest::qWait(200);
    check(w.results()->error.isEmpty() && serializeProject(w.project()) == bytes, "Undo recovers a valid constrained system after a type edit");

    // Removing a source or splitting a constrained edge must preserve the project.
    table->setCurrentCell(1,3);
    bool warning = false;
    QTimer::singleShot(0, &w, [&] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { warning = true; message->accept(); }
    });
    w.findChild<QAbstractButton*>("deleteSurfaceButton")->click();
    check(warning && serializeProject(w.project()) == bytes, "Deleting a referenced surface is rejected transactionally");
    warning = false;
    QTimer::singleShot(0, &w, [&] {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { warning = true; message->accept(); }
    });
    w.findChild<QAbstractButton*>("addSurfaceButton")->click();
    check(warning && serializeProject(w.project()) == bytes, "Insertion does not silently redefine an edge constraint");

    auto pickupOnly = constrained;
    pickupOnly.system.solves.resize(1);
    w.setProject(pickupOnly);
    table->setCurrentCell(1,3);
    w.findChild<QAbstractButton*>("addSurfaceButton")->click();
    check(w.project().system.solves[0].surface == 2 && w.project().system.solves[0].reference == 0, "Insertion reindexes pickup source and target in the GUI");
    table->setCurrentCell(3,3);
    for (auto* b : w.findChildren<QToolButton*>()) if (b->text() == "Вверх") { b->click(); break; }
    check(w.project().system.solves[0].surface == 1 && w.project().system.solves[0].reference == 0, "Moving a surface preserves its pickup identity");
    table->setCurrentCell(2,3);
    w.findChild<QAbstractButton*>("deleteSurfaceButton")->click();
    check(w.project().system.solves.empty(), "Removing a target removes its attached solve");

    w.setProject(constrained);
    edit(1, 4, [&](SolveEditor* d) {
        d->findChild<QComboBox*>("solveType")->setCurrentIndex(3);
        d->findChild<QDoubleSpinBox*>("solveValue")->setValue(60);
        if (!dir.isEmpty()) d->grab().save(dir + "/overall_length.png");
        accept(d);
    });
    close(w.project().system.imageZ(), 60, "Overall length moves the image to the requested coordinate");
    auto defocused = w.project();
    defocused.system.defocus = 2;
    applySolves(defocused.system);
    const auto defocusedBytes = serializeProject(defocused);
    check(serializeProject(deserializeProject(defocusedBytes)) == defocusedBytes,
          "Loading overall length resolves only after the saved defocus has been read");
    if (!dir.isEmpty()) w.grab().save(dir + "/linked_system.png");
    edit(1, 4, [&](SolveEditor* d) { d->findChild<QComboBox*>("solveType")->setCurrentIndex(0); accept(d); });
    check(!isSolved(w.project().system, SolveParameter::Thickness, 1), "Switching to fixed keeps the current value and removes the solve");
    close(w.project().system.imageZ(), 60, "Removing a length constraint preserves its last calculated value");
    QTemporaryDir examples;
    w.writeExamples(examples.path());
    const auto linked = loadProject(examples.path() + "/linked_singlet.optcad");
    check(linked.system.solves.size() == 2 && linked.system.validate(linked.catalog).empty(), "Generated linked lens is complete and physically valid");
    auto stale = linked;
    stale.system.surfaces[0].radius = 70;
    const auto oldRadius = stale.system.surfaces[1].radius;
    saveProject(examples.path() + "/resolved.optcad", stale);
    QFile saved(examples.path() + "/resolved.optcad");
    if (!saved.open(QIODevice::ReadOnly)) throw std::runtime_error("Saved constrained project cannot be opened");
    const auto savedBytes = saved.readAll();
    check(serializeProject(loadProject(examples.path() + "/resolved.optcad")) == savedBytes,
          "Saving writes resolved values even when a C++ caller supplied stale caches");
    check(stale.system.surfaces[1].radius == oldRadius, "Saving a resolved copy does not mutate the caller's model");
    w.setProject(Project{});
    warning = false;
    edit(1, 3, [&](SolveEditor* d) {
        d->findChild<QComboBox*>("solveType")->setCurrentIndex(1);
        d->findChild<QComboBox*>("solveReference")->setCurrentIndex(0);
        d->findChild<QDoubleSpinBox*>("solveScale")->setValue(-1);
        table->item(1,3)->setText("60"); // emulate a delivered external/background edit
        QTimer::singleShot(0, &w, [&] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { warning = true; message->accept(); }
        });
        accept(d);
    });
    check(warning && w.project().system.solves.empty() && w.project().system.surfaces[0].radius == 60,
          "A stale solve editor cannot overwrite geometry changed while its dialog was open");
    w.setProject(Project{});
    edit(1, 3, [&](SolveEditor* d) {
        auto* type = d->findChild<QComboBox*>("solveType");
        const int index = type->findData(int(SolveKind::CurvaturePickup));
        check(index >= 0, "Radius dialog offers curvature pickup separately from radius pickup");
        type->setCurrentIndex(index);
        d->findChild<QComboBox*>("solveReference")->setCurrentIndex(0);
        d->findChild<QDoubleSpinBox*>("solveScale")->setValue(-2);
        d->findChild<QDoubleSpinBox*>("solveOffset")->setValue(.01);
        bool unitLabel = false;
        for (auto* label : d->findChildren<QLabel*>())
            unitLabel |= label->text() == "Смещение кривизны, 1/мм";
        check(unitLabel, "Curvature offset displays reciprocal millimetres");
        if (!dir.isEmpty()) d->grab().save(dir + "/curvature_pickup.png");
        accept(d);
    });
    close(w.project().system.surfaces[1].radius, -100. / 3, "Curvature dialog calculates the expected radius");
    check(!(table->item(2,3)->flags() & Qt::ItemIsEditable) && table->item(2,3)->toolTip().contains("1/мм"),
          "Dependent radius is protected and its tooltip states curvature units");
    const auto curvatureBytes = serializeProject(w.project());
    check(QJsonDocument::fromJson(curvatureBytes).object()["version"] == 3, "Curvature metadata requires project format 3");
    check(serializeProject(deserializeProject(curvatureBytes)) == curvatureBytes,
          "Curvature metadata and scalar geometry survive a roundtrip");
    auto downgraded = QJsonDocument::fromJson(curvatureBytes).object();
    downgraded["version"] = 2;
    rejected = false;
    try { deserializeProject(QJsonDocument(downgraded).toJson()); } catch (const std::exception&) { rejected = true; }
    check(rejected, "Curvature solves cannot be hidden in an older format");
    table->item(1,3)->setText("100");
    QTest::qWait(150);
    close(w.project().system.surfaces[1].radius, -100, "Changing source radius updates curvature pickup through the UI");
    w.findChild<QAction*>("undoAction")->trigger();
    check(QJsonDocument::fromJson(serializeProject(w.project())).object()["sequential"] ==
          QJsonDocument::fromJson(curvatureBytes).object()["sequential"],
          "Undo restores curvature coefficients and dependent radius");
    w.findChild<QAction*>("redoAction")->trigger();
    close(w.project().system.surfaces[1].radius, -100, "Redo recalculates curvature from the restored source");
    edit(1, 3, [&](SolveEditor* d) {
        check(d->findChild<QComboBox*>("solveType")->currentData().toInt() == int(SolveKind::CurvaturePickup),
              "Reopening a curvature dialog selects its saved type");
        close(d->findChild<QDoubleSpinBox*>("solveOffset")->value(), .01,
              "Reopening restores curvature offset precision");
        d->reject();
    });
    edit(1, 3, [&](SolveEditor* d) {
        d->findChild<QComboBox*>("solveType")->setCurrentIndex(0);
        accept(d);
    });
    check(w.project().system.solves.empty() && QJsonDocument::fromJson(serializeProject(w.project())).object()["version"] == 1,
          "Removing the last curvature constraint restores a plain format 1 project");
    close(w.project().system.surfaces[1].radius, -100, "Removing curvature constraint keeps the last resolved radius");
    SolveEditor thicknessDialog(w.project(), 0, SolveParameter::Thickness, &w);
    check(thicknessDialog.findChild<QComboBox*>("solveType")->findData(int(SolveKind::CurvaturePickup)) == -1,
          "Thickness dialog does not offer a curvature solve");
    const auto beforeHelp = serializeProject(w.project());
    auto* helpAction = w.findChild<QAction*>("learningGuideAction");
    check(helpAction != nullptr, "Help menu offers the beginner guide");
    helpAction->trigger();
    auto* guide = w.findChild<QDialog*>("learningGuide");
    check(guide && guide->isVisible(), "Guide opens without a modal editor or external files");
    auto* text = guide->findChild<QTextBrowser*>("learningText");
    check(text && text->toPlainText().contains("first_lens.cpp") && text->toPlainText().contains("Qt Concurrent"),
          "Deployed guide contains the actual source walkthrough and dependency explanations");
    auto* search = guide->findChild<QLineEdit*>("learningSearch");
    search->setText("std::vector");
    guide->findChild<QPushButton*>("learningFindNext")->click();
    check(text->textCursor().selectedText() == "std::vector", "Guide search selects a real occurrence");
    search->setText("no-such-learning-text-1234");
    QTest::keyClick(search, Qt::Key_Return);
    check(guide->findChild<QLabel*>("learningSearchStatus")->text() == "Текст не найден",
          "Guide search reports missing text");
    helpAction->trigger();
    check(w.findChildren<QDialog*>("learningGuide").size() == 1, "Repeated help action reuses the existing guide");
    if (!dir.isEmpty()) guide->grab().save(dir + "/learning_guide.png");
    guide->close();
    QTest::qWait(20);
    check(serializeProject(w.project()) == beforeHelp, "Reading and searching guide never alters the project");
    w.setProject(original);
    return checks;
}
