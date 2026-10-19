#include "window.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
int verifyInstallation(Window&, const QString&);

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--verify-installation")
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QApplication::setApplicationName("Optical CAD");
    QApplication::setOrganizationName("OpticalCAD");
    Window window;
    const auto args = app.arguments();
    if (args.contains("--verify-installation")) {
        const int i = args.indexOf("--verify-installation");
        return i + 1 < args.size() ? verifyInstallation(window, args[i + 1]) : 1;
    }
    if (args.contains("--import-geopter")) {
        int i = args.indexOf("--import-geopter");
        if (i + 1 >= args.size())
            return 1;
        try {
            QFile f(args[i + 1]);
            if (!f.open(QIODevice::ReadOnly))
                return 1;
            auto p = importGeopter(f.readAll());
            int save = args.indexOf("--save-project");
            if (save >= 0 && save + 1 < args.size()) {
                saveProject(args[save + 1], p);
                return 0;
            }
            window.setProject(std::move(p));
        } catch (const std::exception& e) {
            qWarning("%s", e.what());
            return 1;
        }
    }
    if (args.contains("--write-examples")) {
        int i = args.indexOf("--write-examples");
        if (i + 1 < args.size())
            window.writeExamples(args[i + 1]);
        return 0;
    }
    if (args.size() > 1 && !args[1].startsWith("--")) {
        try {
            window.setProject(loadProject(args[1]));
        } catch (const std::exception& e) {
            qWarning("%s", e.what());
            return 1;
        }
    }
    window.show();
    return app.exec();
}
