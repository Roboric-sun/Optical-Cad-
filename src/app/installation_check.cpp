// Acceptance runs inside the deployed executable, against its real UI and SDK.
#include "window.hpp"
#include "python_console.hpp"
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

int verifyInstallation(Window& window, const QString& reportPath) {
    QJsonArray passed;
    auto check = [&](bool condition, const char* name) {
        if (!condition) throw std::runtime_error(name);
        passed.append(QString::fromUtf8(name));
        qInfo("Installation: %s", name);
    };
    auto wait = [&](auto ready) {
        QElapsedTimer time; time.start();
        while (!ready() && time.elapsed() < 60000) {
            QApplication::processEvents(); QThread::msleep(10);
        }
        check(ready(), "Asynchronous operation completed");
    };
    auto command = [&](const char* name) {
        auto* action = window.findChild<QAction*>(name);
        check(action != nullptr, name); action->trigger();
        QApplication::processEvents();
    };
    auto fileDialog = [&](const QString& path, const char* action) {
        bool selected = false;
        bool timedOut = false;
        QTimer deadline; deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &window, [&] {
            timedOut = true;
            for (auto* widget : QApplication::topLevelWidgets())
                if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QFileDialog*>(widget);
                if (!dialog || !dialog->isVisible()) continue;
                selected = true;
                dialog->setOption(QFileDialog::DontUseNativeDialog);
                if (auto* filename = dialog->findChild<QLineEdit*>("fileNameEdit")) filename->setText(path);
                else dialog->selectFile(path);
                qInfo("Installation file selection: %s", qPrintable(dialog->selectedFiles().join(",")));
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                if (!dialog->isVisible()) timer.stop();
            }
        });
        deadline.start(15000); timer.start(30); command(action);
        check(selected && !timedOut, "File dialog operated in deployed application");
    };
    QJsonObject report;
    int result = 0;
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "Fresh project directory available");
        check(!QFontDatabase::families().isEmpty(), "System fonts available");
        window.show(); QApplication::processEvents();
        check(window.results() && window.results()->error.isEmpty(), "Initial GUI analyses succeed");
        auto* table = window.findChild<QTableWidget*>("surfaceTable");
        check(table != nullptr, "Surface editor available");
        const double original = window.project().system.surfaces.front().radius;
        table->item(1, 3)->setText(QString::number(original + 5));
        check(window.project().system.surfaces.front().radius == original + 5, "GUI edit updates optical model");
        command("undoAction");
        check(window.project().system.surfaces.front().radius == original, "GUI undo restores model");
        command("redoAction");
        check(window.project().system.surfaces.front().radius == original + 5, "GUI redo restores edit");
        const QString projectPath = temporary.filePath("accepted.optcad");
        fileDialog(projectPath, "saveAsAction");
        check(serializeProject(loadProject(projectPath)) == serializeProject(window.project()), "GUI save preserves project");
        command("pythonConsoleAction");
        auto* console = dynamic_cast<PythonConsole*>(window.findChild<QDialog*>("pythonConsole"));
        check(console != nullptr, "Packaged Python console opens");
        auto* source = console->findChild<QPlainTextEdit*>("pythonSource");
        auto* run = console->findChild<QPushButton*>("pythonRun");
        auto* stop = console->findChild<QPushButton*>("pythonStop");
        source->setPlainText("project.system['surfaces'][0]['radius'] += 2\nprint(project.analyze()['efl_mm'])");
        run->click(); wait([&] { return run->isEnabled(); });
        check(window.project().system.surfaces.front().radius == original + 7, "Packaged SDK and batch apply Python result");
        command("undoAction");
        check(window.project().system.surfaces.front().radius == original + 5, "Python edit participates in GUI undo");
        console->close(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        command("saveAction");
        fileDialog(projectPath, "openAction");
        check(window.project().system.surfaces.front().radius == original + 5, "GUI reopens saved project");
        command("pythonConsoleAction");
        console = dynamic_cast<PythonConsole*>(window.findChild<QDialog*>("pythonConsole"));
        source = console->findChild<QPlainTextEdit*>("pythonSource");
        run = console->findChild<QPushButton*>("pythonRun");
        stop = console->findChild<QPushButton*>("pythonStop");
        const auto before = serializeProject(window.project());
        const QString ready = temporary.filePath("child-ready");
        const QString marker = temporary.filePath("child-survived");
        const auto literal = [](const QString& text) {
            QByteArray json = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
            return QString::fromUtf8(json.mid(1, json.size() - 2));
        };
        source->setPlainText("import time, subprocess, sys\nfrom pathlib import Path\n"
            "subprocess.Popen([sys.executable, '-c', \"import time; from pathlib import Path; time.sleep(2); Path(\" + "
            + literal(literal(marker)) + " + \").write_text('survived')\"])\n"
            "Path(" + literal(ready) + ").write_text('ready')\n"
            "project.system['surfaces'][0]['radius'] = 999\ntime.sleep(60)");
        run->click(); wait([&] { return QFile::exists(ready); }); stop->click();
        wait([&] { return run->isEnabled(); });
        check(serializeProject(window.project()) == before, "Cancelling packaged Python leaves project unchanged");
        QElapsedTimer cancellation; cancellation.start();
        while (cancellation.elapsed() < 3000) { QApplication::processEvents(); QThread::msleep(10); }
        check(!QFile::exists(marker), "Cancelling Python terminates descendant process");
        console->close();
        report["ok"] = true;
    } catch (const std::exception& e) {
        report["ok"] = false; report["error"] = QString::fromUtf8(e.what()); result = 1;
    }
    report["checks"] = passed;
    QSaveFile output(reportPath);
    const auto bytes = QJsonDocument(report).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return 1;
    return result;
}
