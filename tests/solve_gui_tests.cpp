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
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
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
    w.setProject(original);
    return checks;
}
