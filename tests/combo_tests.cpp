#include "window.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QImage>
#include <QPalette>
#include <QScrollBar>
#include <QTableWidget>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
// Measure the rendered glyphs, not just palette values: the popup delegate can
// resolve a different palette from its owning combo on macOS.
double luminance(QColor c) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
}
bool readableRow(QAbstractItemView* view, const QModelIndex& index) {
    const QRect logical = view->visualRect(index).intersected(view->viewport()->rect());
    const QImage image = view->viewport()->grab().toImage();
    const double scale = image.devicePixelRatio();
    const QRect row(qRound(logical.x() * scale), qRound(logical.y() * scale),
                    qRound(logical.width() * scale), qRound(logical.height() * scale));
    if (row.width() < 30 || row.height() < 8)
        return false;
    const double background = luminance(image.pixelColor(row.right() - qRound(8 * scale), row.center().y()));
    int glyphPixels = 0;
    // Ignore the focus frame and margins; the right half of the row is blank.
    for (int y = row.top() + qRound(4 * scale); y < row.bottom() - qRound(4 * scale); ++y)
        for (int x = row.left() + qRound(6 * scale); x < row.center().x(); ++x) {
            const double ink = luminance(image.pixelColor(x, y));
            const double contrast = (std::max(ink, background) + .05) / (std::min(ink, background) + .05);
            if (contrast >= 3 && ++glyphPixels > 12)
                return true;
        }
    return false;
}
}

size_t comboChecks(Window& w, const QString& dir) {
    size_t checks = 0;
    auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok)
            throw std::runtime_error(message);
    };
    const auto original = w.project();
    const auto appPalette = QApplication::palette();
    const auto windowPalette = w.palette();
    auto* table = w.findChild<QTableWidget*>("surfaceTable");
    for (bool dark : {false, true}) {
        QPalette palette = appPalette;
        for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
            for (auto role : {QPalette::Window, QPalette::Base, QPalette::Button})
                palette.setColor(group, role, dark ? QColor("#282828") : QColor(Qt::white));
            for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
                palette.setColor(group, role, dark ? Qt::white : Qt::black);
            palette.setColor(group, QPalette::HighlightedText, Qt::white);
            palette.setColor(group, QPalette::Highlight, QColor("#005fc0"));
        }
        QApplication::setPalette(palette);
        w.setPalette(palette);
        w.setProject(original);
        QTest::qWait(150);
        auto* kind = qobject_cast<QComboBox*>(table->cellWidget(1, 2));
        check(kind && kind->isVisible(), "Surface type combo visible");
        QTest::mouseClick(kind, Qt::LeftButton, Qt::NoModifier,
                         QPoint(kind->width() - 6, kind->height() / 2));
        check(QTest::qWaitFor([&] { return kind->view()->isVisible(); }), "Surface type popup opens");
        QTest::qWait(50);
        auto* view = kind->view();
        if (!dir.isEmpty())
            view->window()->grab().save(dir + (dark ? "/combo_dark.png" : "/combo_light.png"));
        for (int row = 0; row < kind->count(); ++row)
            check(readableRow(view, kind->model()->index(row, 0)), "Selected and unselected popup labels are readable");
        const auto chosen = kind->model()->index(1, 0);
        // Let Qt's opening-click suppression timer expire before clicking a row.
        QTest::qWait(QApplication::doubleClickInterval() + 50);
        QTest::mouseMove(view->viewport(), view->visualRect(chosen).center());
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(chosen).center());
        QTest::qWait(150);
        check(w.project().system.surfaces[0].kind == optics::SurfaceKind::Mirror,
              "Popup mouse selection changes the surface type");
        check(QApplication::activePopupWidget() == nullptr, "Popup closes after editor rebuild");
        w.findChild<QAction*>("undoAction")->trigger();
        QTest::qWait(150);
        check(w.project().system.surfaces[0].kind == original.system.surfaces[0].kind,
              "Undo restores popup selection");

        auto* material = qobject_cast<QComboBox*>(table->cellWidget(1, 5));
        QTest::mouseClick(material, Qt::LeftButton, Qt::NoModifier,
                         QPoint(material->width() - 6, material->height() / 2));
        check(QTest::qWaitFor([&] { return material->view()->isVisible(); }), "Material popup opens");
        const int target = material->findText("F2");
        check(target >= 0, "Catalog material available");
        view = material->view();
        check(view->verticalScrollBar()->maximum() > 0, "Large glass catalog has a scrollable popup");
        const auto lastGlass = material->model()->index(material->count() - 1, 0);
        view->setCurrentIndex(lastGlass);
        view->scrollTo(lastGlass);
        QTest::qWait(50);
        check(readableRow(view, lastGlass), "End of the glass catalog remains readable after scrolling");
        const auto glass = material->model()->index(target, 0);
        view->setCurrentIndex(glass);
        view->scrollTo(glass);
        QTest::qWait(50);
        if (!dir.isEmpty())
            view->window()->grab().save(dir + (dark ? "/materials_dark.png" : "/materials_light.png"));
        check(readableRow(view, glass), "Scrolled catalog selection is readable");
        QTest::keyClick(view, Qt::Key_Return);
        QTest::qWait(150);
        check(w.project().system.surfaces[0].material == "F2", "Popup keyboard selection updates the glass");
        check(QApplication::activePopupWidget() == nullptr, "Material popup closes without an orphan window");

        material = qobject_cast<QComboBox*>(table->cellWidget(1, 5));
        material->showPopup();
        check(QTest::qWaitFor([&] { return material->view()->isVisible(); }), "Material popup reopens after selection");
        QTest::keyClick(material->view(), Qt::Key_Escape);
        QTest::qWait(50);
        check(QApplication::activePopupWidget() == nullptr && w.project().system.surfaces[0].material == "F2",
              "Escape closes the popup without changing the model");
    }
    QApplication::setPalette(appPalette);
    w.setPalette(windowPalette);
    w.setProject(original);
    return checks;
}
