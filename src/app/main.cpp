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
    QApplication::setApplicationName(OPTICS_DEVELOPER_BUILD?"Optical CAD Developer":"Optical CAD");
    QApplication::setApplicationDisplayName(QApplication::applicationName());
    QApplication::setOrganizationName("OpticalCAD");
    Window window;
    const auto args = app.arguments();
    if (args.contains("--verify-installation")) {
        const int i = args.indexOf("--verify-installation");
        return i + 1 < args.size() ? verifyInstallation(window, args[i + 1]) : 1;
    }
    if (args.contains("--import-geopter") || args.contains("--import-zemax")) {
        const bool zemax=args.contains("--import-zemax");
        int i = args.indexOf(zemax?"--import-zemax":"--import-geopter");
        if (i + 1 >= args.size())
            return 1;
        try {
            QFile f(args[i + 1]);
            if (!f.open(QIODevice::ReadOnly) || f.size()>32*1024*1024)
                return 1;
            auto p = zemax?importZemax(f.readAll()):importGeopter(f.readAll());
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
            if(args[1].endsWith(".zmx",Qt::CaseInsensitive)) {
                QFile input(args[1]);
                if(!input.open(QIODevice::ReadOnly) || input.size()>32*1024*1024) return 1;
                window.setProject(importZemax(input.readAll()));
            } else window.setProject(loadProject(args[1]));
        } catch (const std::exception& e) {
            qWarning("%s", e.what());
            return 1;
        }
    }
    window.show();
    return app.exec();
}
