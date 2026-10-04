#include "office.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <cmath>

void NavigatorTree::resizeEvent(QResizeEvent* event) {
    QTreeWidget::resizeEvent(event);
    setColumnWidth(0, std::clamp(qRound(width() * .55), 130, 260));
}

QIcon officeIcon(const QString& name) {
    QIcon icon;
    for (int pixels : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm(pixels, pixels);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(pixels / 32., pixels / 32.);
        QColor blue("#4389c6"), grey("#7e929e"), green("#55a24d"), orange("#eea43a"),
            red("#d75351");
        p.setPen(QPen(blue, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        auto line = [&](double x, double y, double a, double b) {
            p.drawLine(QPointF(x, y), QPointF(a, b));
        };
        auto poly = [&](std::initializer_list<QPointF> points) {
            p.drawPolygon(QPolygonF(QList<QPointF>(points)));
        };
        if (name == "link") {
            p.setPen(QPen(blue, 2));
            p.drawRoundedRect(QRectF(3, 7, 16, 9), 4, 4);
            p.drawRoundedRect(QRectF(13, 16, 16, 9), 4, 4);
            line(12, 13, 21, 19);
        } else if (name == "lens") {
            QPainterPath path;
            path.moveTo(16, 3);
            path.cubicTo(5, 13, 5, 19, 16, 29);
            path.cubicTo(27, 19, 27, 13, 16, 3);
            p.setBrush(QColor("#c7e1f3"));
            p.drawPath(path);
        } else if (name == "mirror") {
            p.setPen(QPen(grey, 2.7));
            QPainterPath path;
            path.moveTo(21, 3);
            path.cubicTo(6, 9, 6, 23, 21, 29);
            p.drawPath(path);
            p.setPen(QPen(grey, 1.4));
            for (int y : {6, 12, 18, 24})
                line(y == 6 || y == 24 ? 17 : 12, y, y == 6 || y == 24 ? 22 : 17, y - 3);
        } else if (name == "prism") {
            p.setBrush(QColor("#c7e1f3"));
            poly({{16, 3}, {30, 28}, {2, 28}});
        } else if (name == "box" || name == "scene" || name == "step") {
            p.setPen(QPen(name == "step" ? green : QColor("#263640"), 1.5));
            poly({{16, 3}, {28, 9}, {28, 23}, {16, 30}, {4, 23}, {4, 9}});
            line(4, 9, 16, 16);
            line(16, 16, 28, 9);
            line(16, 16, 16, 30);
            if (name == "scene") {
                p.setPen(QPen(red, 1.6));
                line(1, 23, 30, 6);
                p.setPen(QPen(blue, 1.2));
                line(1, 23, 30, 26);
            }
        } else if (name == "sphere") {
            p.setPen(QPen(QColor("#263640"), 1.3));
            p.drawEllipse(QRectF(3, 3, 26, 26));
            p.drawEllipse(QRectF(10, 3, 12, 26));
            p.drawEllipse(QRectF(3, 11, 26, 10));
            line(3, 16, 29, 16);
        } else if (name == "cylinder") {
            p.setPen(QPen(QColor("#263640"), 1.5));
            p.drawEllipse(QRectF(6, 3, 20, 7));
            line(6, 6.5, 6, 25);
            line(26, 6.5, 26, 25);
            p.drawArc(QRectF(6, 21, 20, 8), 180 * 16, 180 * 16);
        } else if (name == "cad" || name == "material") {
            p.setPen(QPen(grey, 1.5));
            p.setBrush(QColor("#e8f0f6"));
            poly({{16, 4}, {27, 10}, {27, 23}, {16, 29}, {5, 23}, {5, 10}});
            if (name == "cad") {
                p.setBrush(Qt::white);
                p.setPen(QPen(green, 1.6));
                p.drawEllipse(QRectF(20, 0, 11, 11));
                line(25.5, 2, 25.5, 9);
                line(22, 5.5, 29, 5.5);
            } else {
                p.setPen(blue);
                p.drawText(QRectF(5, 5, 22, 22), Qt::AlignCenter, "n");
            }
        } else if (name == "source") {
            p.setPen(QPen(orange, 1.8));
            p.setBrush(QColor("#ffc656"));
            p.drawEllipse(QRectF(11, 11, 10, 10));
            for (int i = 0; i < 8; ++i) {
                double a = i * 3.141592653589793 / 4;
                line(16 + 9 * cos(a), 16 + 9 * sin(a), 16 + 14 * cos(a), 16 + 14 * sin(a));
            }
        } else if (name == "rectangleSource" || name == "ellipseSource") {
            p.setPen(QPen(orange, 1.5));
            p.setBrush(QColor("#f6c984"));
            if (name == "ellipseSource")
                p.drawEllipse(QRectF(4, 22, 25, 6));
            else
                p.drawRect(QRectF(4, 22, 25, 6));
            for (int x : {10, 16, 22}) {
                line(x, 20, x - 3, 5);
                line(x - 3, 5, x - 5, 9);
            }
        } else if (name == "catalog") {
            p.setPen(QPen(blue, 1.4));
            p.setBrush(QColor("#e1edf7"));
            for (int x : {4, 13, 22}) {
                p.drawRect(QRectF(x, 4, 7, 24));
                line(x + 2, 6, x + 2, 26);
                line(x + 2, 11, x + 5, 11);
            }
        } else if (name == "grid" || name == "detector" || name == "editor" || name == "image") {
            p.setPen(QPen(name == "detector" || name == "image" ? green : blue, 1.4));
            p.setBrush(QColor("#f8fbfd"));
            p.drawRect(QRectF(3, 4, 26, 24));
            p.fillRect(QRectF(3, 4, 26, 5), name == "detector" ? green : blue);
            for (int i = 1; i < 4; ++i) {
                line(3, 9 + i * 4.75, 29, 9 + i * 4.75);
                line(3 + i * 6.5, 9, 3 + i * 6.5, 28);
            }
        } else if (name == "map" || name == "psf" || name == "wavefront") {
            if (name == "wavefront") {
                p.setBrush(QColor("#39315d"));
                p.drawEllipse(QRectF(3, 3, 26, 26));
            } else
                p.fillRect(QRectF(3, 3, 26, 26), QColor("#39315d"));
            for (int i = 0; i < 5; ++i) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor::fromHsv(23 + 6 * i, 150 - i * 25, 175 + i * 18));
                p.drawEllipse(QRectF(5 + i * 2, 5 + i * 2, 22 - i * 4, 22 - i * 4));
            }
        } else if (name == "folder") {
            p.setPen(QPen(QColor("#b2843b"), 1.3));
            p.setBrush(QColor("#efbd61"));
            poly({{2, 7}, {12, 7}, {16, 11}, {30, 11}, {30, 27}, {2, 27}});
        } else if (name == "eye") {
            p.setPen(QPen(grey, 1.6));
            QPainterPath path;
            path.moveTo(2, 16);
            path.quadTo(16, 3, 30, 16);
            path.quadTo(16, 29, 2, 16);
            p.drawPath(path);
            p.setBrush(grey);
            p.drawEllipse(QRectF(12, 12, 8, 8));
        } else if (name == "hidden" || name == "circle" || name == "stop" || name == "field" ||
                   name == "aperture") {
            p.setPen(QPen(
                name == "stop" ? red : (name == "circle" || name == "hidden" ? grey : blue), 1.6));
            p.drawEllipse(QRectF(5, 5, 22, 22));
            if (name == "hidden")
                line(7, 25, 25, 7);
            if (name == "field" || name == "stop") {
                p.setBrush(name == "stop" ? red : blue);
                p.drawEllipse(QRectF(12, 12, 8, 8));
            }
            if (name == "aperture")
                for (int i = 0; i < 6; ++i) {
                    double a = i * 3.141592653589793 / 3;
                    line(16 + 11 * cos(a), 16 + 11 * sin(a), 16 + 5 * cos(a + .6),
                         16 + 5 * sin(a + .6));
                }
        } else if (name == "ray" || name == "fan" || name == "trace" || name == "layout" ||
                   name == "paraxial") {
            if (name == "layout") {
                p.drawRect(QRectF(2, 5, 28, 22));
                p.setBrush(QColor("#c7e1f3"));
                p.drawEllipse(QRectF(10, 7, 7, 18));
            }
            p.setPen(QPen(blue, 1.4));
            line(3, 24, 28, 16);
            p.setPen(QPen(green, 1.4));
            line(3, 6, 28, 16);
            p.setPen(QPen(red, 1.4));
            line(3, 15, 28, 16);
            p.setBrush(orange);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(0, 12, 6, 6));
        } else if (name == "axes" || name == "tilt" || name == "scatter" || name == "coating") {
            if (name == "tilt") {
                p.drawArc(QRectF(6, 5, 20, 20), 20 * 16, 280 * 16);
                poly({{8, 25}, {3, 20}, {10, 19}});
            } else {
                p.setPen(QPen(blue, 1.5));
                line(8, 26, 8, 4);
                p.setPen(QPen(green, 1.5));
                line(8, 26, 28, 26);
                p.setPen(QPen(red, 1.5));
                line(8, 26, 26, 8);
                if (name == "scatter") {
                    line(8, 26, 30, 14);
                    line(8, 26, 30, 20);
                    line(8, 26, 22, 3);
                }
            }
        } else if (name == "wave" || name == "opd" || name == "mtf" || name == "optimize" ||
                   name == "rms") {
            p.setPen(QPen(name == "wave" ? red : blue, 1.6));
            QPolygonF curve;
            for (int x = 2; x <= 30; ++x)
                curve << QPointF(x, name == "mtf" ? 28 - 24 * std::pow(1 - (x - 2) / 28., 1.6)
                                                  : 16 + 8 * std::sin((x - 2) / 28. * 6.283185307));
            p.drawPolyline(curve);
            if (name == "mtf") {
                p.setPen(QPen(grey, 1));
                line(2, 3, 2, 29);
                line(2, 29, 31, 29);
            }
        } else if (name == "polarization") {
            for (int x : {8, 16, 24})
                line(x, 3, x, 29);
        } else if (name == "thermo") {
            p.setPen(QPen(grey, 1.4));
            p.drawRoundedRect(QRectF(13, 3, 6, 22), 3, 3);
            p.setBrush(red);
            p.setPen(red);
            p.drawEllipse(QRectF(10, 21, 12, 10));
            line(16, 11, 16, 25);
        } else if (name == "check") {
            p.setPen(QPen(green, 1.6));
            p.drawEllipse(QRectF(3, 3, 26, 26));
            line(9, 16, 14, 21);
            line(14, 21, 24, 10);
        } else if (name == "zoomIn" || name == "zoomOut" || name == "search") {
            p.drawEllipse(QRectF(4, 3, 18, 18));
            line(20, 20, 29, 29);
            if (name != "search")
                line(8, 12, 18, 12);
            if (name == "zoomIn")
                line(13, 7, 13, 17);
        } else if (name == "undo" || name == "redo" || name == "rotate") {
            if (name == "redo") {
                p.translate(32, 0);
                p.scale(-1, 1);
            }
            QPainterPath path;
            path.moveTo(27, 24);
            path.cubicTo(27, 7, 15, 5, 7, 12);
            p.drawPath(path);
            poly({{8, 5}, {4, 14}, {15, 14}});
        } else if (name == "fit" || name == "split" || name == "save" || name == "add" ||
                   name == "remove" || name == "import" || name == "settings" || name == "nd" ||
                   name == "rays") {
            p.drawRoundedRect(QRectF(4, 4, 24, 24), 2, 2);
            if (name == "add" || name == "remove") {
                p.setPen(QPen(name == "add" ? green : red, 2));
                line(10, 16, 22, 16);
                if (name == "add")
                    line(16, 10, 16, 22);
            } else if (name == "split") {
                line(16, 4, 16, 28);
                line(4, 12, 28, 12);
            } else if (name == "save") {
                p.setBrush(blue);
                p.drawRect(QRectF(9, 5, 14, 7));
                p.drawRect(QRectF(9, 18, 14, 10));
            } else if (name == "fit") {
                line(8, 12, 8, 8);
                line(8, 8, 12, 8);
                line(24, 20, 24, 24);
                line(24, 24, 20, 24);
            } else if (name == "import") {
                p.setPen(QPen(green, 1.7));
                line(1, 16, 20, 16);
                line(16, 12, 20, 16);
                line(16, 20, 20, 16);
            } else if (name == "settings") {
                p.drawEllipse(QRectF(10, 10, 12, 12));
                for (int x : {8, 16, 24}) {
                    line(x, 0, x, 5);
                    line(x, 27, x, 32);
                }
            } else {
                QFont f = p.font();
                f.setPixelSize(16);
                p.setFont(f);
                p.drawText(QRectF(4, 4, 24, 24), Qt::AlignCenter, name == "nd" ? "n" : "N");
            }
        } else if (name == "up" || name == "down") {
            if (name == "down") {
                p.translate(0, 32);
                p.scale(1, -1);
            }
            line(16, 27, 16, 5);
            line(16, 5, 7, 14);
            line(16, 5, 25, 14);
        } else if (name == "spot") {
            p.drawEllipse(QRectF(3, 3, 26, 26));
            p.setBrush(blue);
            for (int i = 0; i < 14; ++i) {
                double a = i * 2.4, r = 2 + std::sqrt(i) * 2;
                p.drawEllipse(QPointF(16 + r * cos(a), 16 + r * sin(a)), 1.2, 1.2);
            }
        } else if (name == "focus") {
            p.setPen(QPen(green, 1.2));
            line(16, 2, 16, 30);
            p.setPen(QPen(blue, 1.4));
            line(2, 7, 29, 25);
            line(2, 25, 29, 7);
            p.setBrush(red);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(13, 13, 6, 6));
        } else {
            p.drawRect(QRectF(6, 5, 20, 23));
            line(10, 12, 22, 12);
            line(10, 17, 22, 17);
            line(10, 22, 18, 22);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QString officeStyle() {
    return QStringLiteral(R"(
QMainWindow { background: #f7fafd; color: #243e58; }
QWidget { font-family: Arial; font-size: 12px; }
QWidget#officeTitle { background: #245f99; }
QLabel#documentTitle { color: white; font-weight: bold; font-size: 13px; }
QToolButton#windowButton { color: white; background: transparent; border: 0; padding: 0; }
QToolButton#windowButton:hover { background: #4c7eae; }
QWidget#ribbonHeader, QWidget#ribbonContents { background: #f1f5f9; }
QTabBar#modeTabs::tab { background: #f1f5f9; color: #253c52; border: 0; padding: 5px 10px; font-size: 11px; }
QTabBar#modeTabs::tab:selected { background: white; color: #205c98; border: 1px solid #c6d8e8; border-bottom: 0; font-weight: bold; }
QTabBar#modeTabs::tab:disabled { color: #8c9dad; }
QWidget#ribbonGroup { border-right: 1px solid #d3e0eb; }
QLabel#groupCaption { color: #7c8b9a; font-size: 10px; }
QToolButton#fileTab { background: #f1f5f9; border: 0; padding: 4px 14px; }
QToolButton[ribbonCommand="true"] { border: 1px solid transparent; border-radius: 2px; background: transparent; color: #233f59; padding: 1px; font-size: 12px; }
QToolButton[ribbonCommand="true"]:hover { background: #e0ecf7; border-color: #b4cee3; }
QToolButton[ribbonCommand="true"]:pressed, QToolButton[ribbonCommand="true"]:checked { background: #caddf0; border-color: #a0bfdb; }
QToolButton[ribbonCommand="true"]:disabled { color: #9daab6; }
QToolBar { background: #f7fafd; border: 0; border-bottom: 1px solid #d3dfeb; spacing: 3px; padding: 3px; }
QToolButton { border: 1px solid #c5d8e8; border-radius: 2px; background: white; padding: 2px; }
QToolButton:hover { background: #e1eef8; }
QToolButton:checked { background: #d5e6f5; }
QDockWidget::title { background: #e8eff7; color: #245c91; padding: 4px 9px; font-weight: bold; }
QLabel#navigatorTitle { background: #2d70ae; color: white; padding: 4px 8px; font-weight: bold; letter-spacing: .5px; }
QTreeWidget, QTableWidget { border: 1px solid #d3dfeb; background: white; alternate-background-color: #f2f6fa; selection-background-color: #cce2f4; selection-color: #1e405d; }
QHeaderView::section { background: #edf3f8; border: 0; border-right: 1px solid #e0e8ef; border-bottom: 1px solid #d3dfeb; padding: 4px; font-weight: bold; color: #193952; }
QTableWidget { gridline-color: #e2eaf2; }
QTableWidget::item, QTreeWidget::item { padding: 2px; }
QTabWidget::pane { border: 1px solid #d0deec; background: white; }
QTabBar::tab { background: #e8eff7; color: #718398; border: 0; border-right: 1px solid #d0deec; padding: 5px 10px; font-size: 11px; }
QTabBar::tab:selected { background: #fff; color: #245f99; font-weight: bold; }
QTabBar::close-button { image: none; }
QStatusBar { background: #2d70ae; color: white; padding: 1px 6px; }
QStatusBar QLabel { color: white; font-size: 11px; }
QPushButton { background: #fafcfe; border: 1px solid #c9d9e6; border-radius: 2px; padding: 5px 8px; color: #284d6e; }
QPushButton:hover { background: #dfedf9; border-color: #70a5d2; }
QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox { padding: 2px; border: 1px solid #bed0df; background: white; color: #243e58; selection-background-color: #cce2f4; selection-color: #1e405d; }
/* Use the list delegate: the menu delegate draws the combo's white background
   over its selected row, hiding the highlighted label. */
QComboBox { combobox-popup: 0; }
QComboBox QAbstractItemView { background: white; color: #243e58; border: 1px solid #bed0df; selection-background-color: #cce2f4; selection-color: #1e405d; outline: 0; }
QComboBox QAbstractItemView::item { min-height: 20px; padding: 2px 6px; }
QComboBox QAbstractItemView::item:selected { background: #cce2f4; color: #1e405d; }
QScrollBar:horizontal { height: 9px; background: #f2f6fa; }
QScrollBar:vertical { width: 9px; background: #f2f6fa; }
QScrollBar::handle { background: #b9cbdc; border-radius: 3px; min-width: 25px; min-height: 25px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
)");
}

RibbonGroup::RibbonGroup(const QString& title, QWidget* parent) : QWidget(parent) {
    setObjectName("ribbonGroup");
    setProperty("groupName", title);
    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(2, 3, 4, 0);
    box->setSpacing(1);
    auto* body = new QWidget;
    commands_ = new QHBoxLayout(body);
    commands_->setContentsMargins(0, 0, 0, 0);
    commands_->setSpacing(1);
    box->addWidget(body, 1);
    auto* caption = new QLabel(title);
    caption->setObjectName("groupCaption");
    caption->setAlignment(Qt::AlignCenter);
    box->addWidget(caption);
}
QToolButton* RibbonGroup::large(const QString& icon, const QString& text, std::function<void()> fn,
                                const QString& id) {
    auto* b = new QToolButton;
    b->setObjectName(id);
    b->setProperty("ribbonCommand", true);
    b->setProperty("largeCommand", true);
    b->setIcon(officeIcon(icon));
    b->setIconSize({32, 32});
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setMinimumHeight(64);
    b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    commands_->addWidget(b);
    if (fn)
        connect(b, &QToolButton::clicked, this, std::move(fn));
    return b;
}
QVBoxLayout* RibbonGroup::column() {
    auto* column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    commands_->addLayout(column);
    return column;
}
QToolButton* RibbonGroup::small(QVBoxLayout* column, const QString& icon, const QString& text,
                                std::function<void()> fn, const QString& id) {
    auto* b = new QToolButton;
    b->setObjectName(id);
    b->setProperty("ribbonCommand", true);
    b->setIcon(officeIcon(icon));
    b->setIconSize({16, 16});
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setFixedHeight(23);
    b->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    column->addWidget(b, 0, Qt::AlignLeft);
    if (fn)
        connect(b, &QToolButton::clicked, this, std::move(fn));
    return b;
}
OfficeTitleBar::OfficeTitleBar(QMainWindow* window) : QWidget(window), window_(window) {
    setObjectName("officeTitle");
    setFixedHeight(29);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 0, 3, 0);
    row->setSpacing(3);
    auto* logo = new QLabel("C");
    logo->setFixedSize(18, 18);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(
        "background:#e5eef7;color:#245f99;border-radius:3px;font-weight:bold;font-size:14px");
    row->addWidget(logo);
    auto* save = new QToolButton;
    save->setIcon(officeIcon("save"));
    save->setIconSize({14, 14});
    save->setToolTip("Сохранить проект");
    save->setObjectName("titleSaveButton");
    row->addWidget(save);
    title_ = new QLabel;
    title_->setObjectName("documentTitle");
    title_->setAlignment(Qt::AlignCenter);
    row->addWidget(title_, 1);
    for (auto text : {QString("—"), QString("□"), QString("×")}) {
        auto* button = new QToolButton;
        button->setObjectName("windowButton");
        button->setText(text);
        button->setFixedSize(30, 27);
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, text] {
            if (text == "×")
                window_->close();
            else if (text == "—")
                window_->showMinimized();
            else if (window_->isMaximized())
                window_->showNormal();
            else
                window_->showMaximized();
        });
    }
    connect(window, &QWidget::windowTitleChanged, this, &OfficeTitleBar::setTitle);
}
void OfficeTitleBar::setTitle(const QString& text) {
    title_->setText(text);
}
void OfficeTitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && window_->windowHandle())
        window_->windowHandle()->startSystemMove();
}
void OfficeTitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (window_->isMaximized())
            window_->showNormal();
        else
            window_->showMaximized();
    }
}
