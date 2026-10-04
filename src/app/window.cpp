#include "window.hpp"
#include "learning_help.hpp"
#include "office.hpp"
#include "optimization_editor.hpp"
#include "solve_editor.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrent>
#include <algorithm>
#include <sstream>

using namespace optics;
static QString str(const std::string& v) {
    return QString::fromStdString(v);
}
static QString num(double v) {
    return QString::number(v, 'g', 10);
}
static const QStringList surfaceKinds = {"Стандартная", "Зеркальная", "Диафрагма"};
static const QStringList objectKinds = {"Линза",   "Зеркало",  "Сфера", "Тело",
                                        "Цилиндр", "Детектор", "Призма"};
static const QStringList interactions = {"Преломление", "Поглощение", "Отражение", "Рассеяние"};
static const QStringList shapes = {"Точечный", "Прямоугольный", "Эллиптический"};
static const QStringList distributions = {"Конусное", "Косинусное", "Изотропное"};
static QDoubleSpinBox* spin(double v, double low, double high, int digits = 4) {
    auto* s = new QDoubleSpinBox;
    s->setDecimals(digits);
    s->setRange(low, high);
    s->setValue(v);
    return s;
}
static QSpinBox* integerSpin(int value, int lo, int hi) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(value);
    return s;
}
static void configureTable(QTableWidget* t, const QStringList& headers) {
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setAlternatingRowColors(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setDefaultSectionSize(108);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setShowGrid(true);
}
Window::Window(QWidget* parent)
    : QMainWindow(parent), cancel_(std::make_shared<std::atomic<bool>>(false)) {
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    resize(1440, 920);
    setMinimumSize(1100, 720);
    QApplication::setStyle("Fusion");
    setStyleSheet(officeStyle());
    setWindowTitle("Optical CAD — Оптический модуль — " + str(project_.system.name));
    setDockNestingEnabled(true);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    auto* file = new QMenu("Файл", this);
    auto* edit = new QMenu("Правка", this);
    auto* viewMenu = new QMenu("Рабочая область", this);
    auto* help = new QMenu("Справка", this);
    auto action = [&](QMenu* menu, QString text, QKeySequence shortcut, std::function<void()> fn) {
        auto* a = menu->addAction(text);
        a->setShortcut(shortcut);
        addAction(a);
        connect(a, &QAction::triggered, this, std::move(fn));
        return a;
    };
    action(file, "Новый проект", QKeySequence::New, [this] {
        if (!mayDiscard())
            return;
        setProject(Project{});
        path_.clear();
    });
    action(file, "Открыть…", QKeySequence::Open, [this] { open(); });
    action(file, "Сохранить", QKeySequence::Save, [this] { save(); });
    action(file, "Сохранить как…", QKeySequence::SaveAs, [this] { save(true); });
    file->addSeparator();
    action(file, "Выгрузить анализ в CSV…", {}, [this] { exportCSV(); });
    action(file, "Импорт Geopter JSON…", {}, [this] {
        if (!mayDiscard())
            return;
        auto path = QFileDialog::getOpenFileName(this, "Импорт Geopter", {}, "Geopter (*.json)");
        if (path.isEmpty())
            return;
        try {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
                throw std::invalid_argument("Невозможно прочитать файл");
            auto p = importGeopter(file.readAll(), project_.catalog);
            setProject(std::move(p));
            path_.clear();
            dirty_ = true;
            QMessageBox::information(
                this, "Импорт Geopter",
                "Система загружена. Длины волн переведены из нм в мкм; отсутствующие световые "
                "полудиаметры оценены по оболочке лучей. Материалы nd:Vd используют приближение "
                "Коши. Сохраните результат в .optcad.");
        } catch (const std::exception& e) {
            error(e);
        }
    });
    action(edit, "Отменить", QKeySequence::Undo, [this] { undo(); })->setObjectName("undoAction");
    action(edit, "Повторить", QKeySequence::Redo, [this] { undo(true); })->setObjectName("redoAction");
    auto* analysisMenu = new QMenu("Анализ", this);
    action(analysisMenu, "Одиночный луч…", {}, [this] { rayReport(); });
    action(analysisMenu, "Параксиальная трассировка", {}, [this] { paraxialReport(); });
    action(analysisMenu, "Описание системы", {}, [this] { prescription(); });
    for (auto v : {View::Layout, View::Spot, View::Fan, View::OPD, View::Wavefront, View::PSF,
                   View::MTF, View::RMS, View::Longitudinal, View::FieldCurvature,
                   View::ChromaticFocus, View::GeometricMTF})
        action(analysisMenu, viewName(v), {}, [this, v] {
            if (project_.mode != 0)
                changeMode(0);
            addView(v);
        });
    action(help, "Возможности и ограничения", {}, [this] {
        QMessageBox::information(
            this, "Optical CAD 0.7",
            "Собственное C++ ядро · геометрические единицы мм, длины волн мкм, мощность "
            "Вт.\n\nПоследовательный режим: преломление, сферы, коники, асферика A4…A10, "
            "децентрировка, наклон, автофокус, пятно, OPD, волновой фронт, скалярные "
            "монохроматические PSF и MTF, тангенциальные и сагиттальные ray "
            "fan, настраиваемая оптимизация.\n\nНепоследовательный: примитивы, линза и треугольные призмы, источники, "
            "отражение, преломление, диффузное рассеяние, детекторы и баланс "
            "мощности, профили и статистика пятна.\n\nSTEP/IGES, ОПАЛ, поляризация, смешанная трассировка и интеграция с САРУС "
            "ещё не реализованы. Вложенные и пересекающиеся прозрачные тела не "
            "поддерживаются.\n\nPSF/MTF используют приближение Фраунгофера на входном зрачке и "
            "требуют центрированной системы; проверяйте его применимость. Каталожные модели без "
            "температуры/давления. Полное соответствие Geopter пока не подтверждено.");
    });
    action(help, "Как устроена программа — изучаем C++", QKeySequence::HelpContents,
           [this] { showLearningGuide(this); })->setObjectName("learningGuideAction");
    // One Office-style ribbon: title, context tabs, commands, group captions.
    auto* header = new QWidget;
    header->setObjectName("ribbonHeader");
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);
    auto* titleBar = new OfficeTitleBar(this);
    connect(titleBar->findChild<QToolButton*>("titleSaveButton"), &QToolButton::clicked, this,
            [this] { save(); });
    titleBar->setTitle(windowTitle());
    headerLayout->addWidget(titleBar);
    auto* tabsRow = new QHBoxLayout;
    tabsRow->setContentsMargins(0, 0, 4, 0);
    tabsRow->setSpacing(0);
    auto* fileTab = new QToolButton;
    fileTab->setObjectName("fileTab");
    fileTab->setText("Файл");
    fileTab->setMenu(file);
    fileTab->setPopupMode(QToolButton::InstantPopup);
    file->addSeparator();
    file->addMenu(edit);
    file->addMenu(viewMenu);
    file->addMenu(analysisMenu);
    file->addMenu(help);
    tabsRow->addWidget(fileTab);
    modes_ = new QTabBar;
    modes_->setObjectName("modeTabs");
    modes_->setExpanding(false);
    modes_->setDrawBase(false);
    for (const auto& name :
         {"Последовательный режим", "Непоследовательный режим", "Анализ", "Оптимизация", "Допуски",
          "Библиотеки", "Макросы", "Визуализация", "Управление требованиями"})
        modes_->addTab(name);
    for (int i : {4, 6, 8}) {
        modes_->setTabEnabled(i, false);
        modes_->setTabToolTip(i, "Этот раздел ещё не реализован");
    }
    tabsRow->addWidget(modes_);
    tabsRow->addStretch();
    auto* helpButton = new QToolButton;
    helpButton->setText("?");
    helpButton->setMenu(help);
    helpButton->setPopupMode(QToolButton::InstantPopup);
    helpButton->setToolTip("Справка");
    tabsRow->addWidget(helpButton);
    headerLayout->addLayout(tabsRow);
    auto* scroll = new QScrollArea;
    scroll->setObjectName("ribbonScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(94);
    ribbon_ = new QWidget;
    ribbon_->setObjectName("ribbonContents");
    scroll->setWidget(ribbon_);
    headerLayout->addWidget(scroll);
    setMenuWidget(header);

    views_ = new QTabWidget;
    views_->setObjectName("analysisTabs");
    views_->setMovable(true);
    views_->setTabsClosable(true);
    setCentralWidget(views_);
    connect(views_, &QTabWidget::tabCloseRequested, this, [this](int i) {
        if (views_->widget(i) == editorPage_)
            return;
        if (views_->count() > 1) {
            auto* w = views_->widget(i);
            views_->removeTab(i);
            w->deleteLater();
            rebuildTree();
        }
    });
    plotBar_ = new QToolBar;
    plotBar_->setObjectName("plotToolbar");
    plotBar_->setMovable(false);
    plotBar_->setIconSize({16, 16});
    plotBar_->setFixedHeight(29);
    auto tool = [&](QString icon, QString tip, std::function<void()> fn, QString id = {}) {
        auto* a = plotBar_->addAction(officeIcon(icon), tip);
        a->setObjectName(id);
        connect(a, &QAction::triggered, this, std::move(fn));
        return a;
    };
    tool("undo", "Отменить", [this] { undo(); });
    tool("redo", "Повторить", [this] { undo(true); });
    tool("zoomIn", "Увеличить", [this] {
        if (auto* p = dynamic_cast<PlotWidget*>(views_->currentWidget())) {
            p->zoom = std::min(8., p->zoom * 1.25);
            p->update();
        }
    });
    tool("zoomOut", "Уменьшить", [this] {
        if (auto* p = dynamic_cast<PlotWidget*>(views_->currentWidget())) {
            p->zoom = std::max(.2, p->zoom / 1.25);
            p->update();
        }
    });
    tool("fit", "Вписать в окно", [this] {
        if (auto* p = dynamic_cast<PlotWidget*>(views_->currentWidget()))
            p->resetView();
    });
    auto* grid = tool("grid", "Сетка", [] {});
    grid->setCheckable(true);
    grid->setChecked(true);
    connect(grid, &QAction::toggled, this, [this](bool on) {
        for (auto* p : findChildren<PlotWidget*>()) {
            p->grid = on;
            p->update();
        }
    });
    tool("split", "Показать нижнюю панель", [this] {
        auto* dock = project_.mode ? detectorDock_ : editorDock_;
        dock->setVisible(!dock->isVisible());
    });
    tool("rotate", "Стандартный вид", [this] {
        if (auto* p = dynamic_cast<PlotWidget*>(views_->currentWidget())) {
            p->yaw = 0;
            p->pitch = 0;
            p->update();
        }
    });
    tool("settings", "Отделить окно", [this] {
        auto* p = dynamic_cast<PlotWidget*>(views_->currentWidget());
        if (!p)
            return;
        auto* dock = new QDockWidget(viewName(p->view), this);
        dock->setAttribute(Qt::WA_DeleteOnClose);
        auto* copy = new PlotWidget(p->view);
        copy->setData(results_);
        dock->setWidget(copy);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->setFloating(true);
        dock->resize(750, 560);
        dock->show();
    });
    plotBar_->addSeparator();
    scaleLabel_ = new QLabel("Масштаб:");
    plotBar_->addWidget(scaleLabel_);
    scaleBox_ = new QDoubleSpinBox;
    scaleBox_->setObjectName("viewScale");
    scaleBox_->setRange(.2, 8);
    scaleBox_->setValue(1);
    scaleBox_->setSingleStep(.1);
    scaleBox_->setDecimals(1);
    scaleBox_->setFixedWidth(64);
    plotBar_->addWidget(scaleBox_);
    connect(scaleBox_, &QDoubleSpinBox::valueChanged, this, [this](double scale) {
        if (auto* p = dynamic_cast<PlotWidget*>(views_->currentWidget())) {
            p->zoom = scale;
            p->update();
        }
    });
    displayLabel_ = new QLabel("Лучей отображается:");
    plotBar_->addWidget(displayLabel_);
    displayRays_ = new QSpinBox;
    displayRays_->setObjectName("displayRayCount");
    displayRays_->setRange(0, 1000);
    displayRays_->setFixedWidth(64);
    plotBar_->addWidget(displayRays_);
    connect(displayRays_, &QSpinBox::valueChanged, this, [this](int n) {
        if (building_)
            return;
        project_.scene.displayRays = n;
        dirty_ = true;
        updatePlots();
    });
    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    plotBar_->addWidget(spacer);
    fieldBox_ = new QComboBox;
    fieldBox_->setObjectName("fieldCombo");
    fieldBox_->setToolTip("Поле зрения для анализа");
    plotBar_->addWidget(fieldBox_);
    connect(fieldBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (building_ || i < 0)
            return;
        field_ = i;
        recalculate();
    });
    detectorBox_ = new QComboBox;
    detectorBox_->setObjectName("detectorCombo");
    detectorBox_->setToolTip("Выбранный детектор");
    plotBar_->addWidget(detectorBox_);
    connect(detectorBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (results_) {
            results_->detector = std::max(0, i);
            updatePlots();
        }
    });
    connect(views_, &QTabWidget::currentChanged, this, [this](int) { activateWorkspace(); });

    navigator_ = new QDockWidget(this);
    navigator_->setObjectName("navigatorDock");
    auto* navigatorTitle = new QLabel("Навигатор оптической системы");
    navigatorTitle->setObjectName("navigatorTitle");
    navigator_->setTitleBarWidget(navigatorTitle);
    auto* navBody = new QWidget;
    auto* navLayout = new QVBoxLayout(navBody);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);
    auto* searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(4, 4, 4, 4);
    searchRow->setSpacing(3);
    treeSearch_ = new QLineEdit;
    treeSearch_->setObjectName("treeSearch");
    treeSearch_->setPlaceholderText("Поиск по дереву");
    treeSearch_->setClearButtonEnabled(true);
    treeSearch_->setFixedHeight(23);
    searchRow->addWidget(treeSearch_, 1);
    auto navTool = [&](QString icon, QString tip, std::function<void()> fn) {
        auto* b = new QToolButton;
        b->setIcon(officeIcon(icon));
        b->setIconSize({14, 14});
        b->setFixedSize(23, 23);
        b->setToolTip(tip);
        searchRow->addWidget(b);
        connect(b, &QToolButton::clicked, this, std::move(fn));
    };
    navTool("settings", "Параметры системы", [this] {
        if (project_.mode)
            calculationParameters();
        else
            parameters();
    });
    navTool("up", "Переместить поверхность вверх", [this] {
        if (!project_.mode)
            moveSurface(-1);
    });
    navTool("add", "Развернуть дерево", [this] { tree_->expandAll(); });
    navTool("remove", "Свернуть дерево", [this] { tree_->collapseAll(); });
    navTool("split", "Колонки состояния", [this] {
        bool hidden = tree_->isColumnHidden(2);
        tree_->setColumnHidden(2, !hidden);
        tree_->setColumnHidden(3, !hidden);
    });
    navLayout->addLayout(searchRow);
    tree_ = new NavigatorTree;
    tree_->setObjectName("systemTree");
    tree_->setHeaderLabels({"Имя", "Значение", "", ""});
    tree_->setAlternatingRowColors(true);
    tree_->setIndentation(14);
    tree_->setIconSize({14, 14});
    tree_->setColumnWidth(0, 160);
    tree_->setColumnWidth(1, 102);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    tree_->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    tree_->setColumnWidth(2, 23);
    tree_->setColumnWidth(3, 23);
    tree_->headerItem()->setIcon(2, officeIcon("eye"));
    tree_->headerItem()->setIcon(3, officeIcon("circle"));
    navLayout->addWidget(tree_, 1);
    navigator_->setWidget(navBody);
    addDockWidget(Qt::LeftDockWidgetArea, navigator_);
    connect(treeSearch_, &QLineEdit::textChanged, this, [this] { filterTree(); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (building_ || !item)
            return;
        if (item->data(0, Qt::UserRole).isValid())
            selectRow(item->data(0, Qt::UserRole).toInt());
        if (item->data(0, Qt::UserRole + 2).isValid()) {
            building_ = true;
            sources_->selectRow(item->data(0, Qt::UserRole + 2).toInt());
            building_ = false;
        }
    });
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column != 2 || !item->data(0, Qt::UserRole).isValid())
            return;
        int index = item->data(0, Qt::UserRole).toInt();
        if (hidden_.contains(index))
            hidden_.remove(index);
        else
            hidden_.insert(index);
        item->setIcon(2, officeIcon(hidden_.contains(index) ? "hidden" : "eye"));
        updatePlots();
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item->data(0, Qt::UserRole + 1).toString() == "parameters") {
            if (project_.mode)
                calculationParameters();
            else
                parameters();
        } else if (item->data(0, Qt::UserRole + 2).isValid()) {
            showEditor();
            editors_->setCurrentWidget(sources_);
        } else if (item->data(0, Qt::UserRole + 4).isValid())
            addView(View(item->data(0, Qt::UserRole + 4).toInt()));
        else if (item->data(0, Qt::UserRole).isValid())
            showEditor();
    });

    editorDock_ = new QDockWidget("Редактор поверхностей", this);
    editorDock_->setObjectName("editorDock");
    editors_ = new QTabWidget;
    editors_->setObjectName("editorTabs");
    surfaces_ = new QTableWidget;
    objects_ = new QTableWidget;
    sources_ = new QTableWidget;
    surfaces_->setObjectName("surfaceTable");
    objects_->setObjectName("objectTable");
    sources_->setObjectName("sourceTable");
    editorDock_->setWidget(editors_);
    addDockWidget(Qt::BottomDockWidgetArea, editorDock_);
    detectorDock_ = new QDockWidget("Карта облучённости — Детектор 1", this);
    detectorDock_->setObjectName("detectorDock");
    detectorPlot_ = new PlotWidget(View::Detector);
    detectorPlot_->officePresentation = true;
    detectorPlot_->setMinimumHeight(160);
    detectorDock_->setWidget(detectorPlot_);
    addDockWidget(Qt::BottomDockWidgetArea, detectorDock_);
    detectorDock_->hide();
    for (auto* dock : {editorDock_, detectorDock_}) {
        auto* title = new QWidget;
        title->setStyleSheet("background:#e8eff7");
        auto* row = new QHBoxLayout(title);
        row->setContentsMargins(9, 3, 5, 3);
        row->setSpacing(4);
        auto* label = new QLabel(dock->windowTitle());
        label->setStyleSheet("color:#245f99;font-weight:bold;font-size:11px");
        row->addWidget(label, 1);
        auto* close = new QToolButton;
        close->setText("×");
        close->setFixedSize(14, 14);
        close->setStyleSheet("border:0;background:transparent;color:#7c8fa1;padding:0");
        row->addWidget(close);
        connect(close, &QToolButton::clicked, dock, &QDockWidget::hide);
        connect(dock, &QWidget::windowTitleChanged, label, &QLabel::setText);
        dock->setTitleBarWidget(title);
    }
    configureTable(surfaces_, {"№",
                               "Комментарий",
                               "Тип",
                               "Радиус, мм",
                               "Толщина, мм",
                               "Материал",
                               "Полудиам., мм",
                               "Коника",
                               "ΔX, мм",
                               "ΔY, мм",
                               "ΔZ, мм",
                               "Rx, °",
                               "Ry, °",
                               "Rz, °",
                               "T",
                               "R",
                               "A4",
                               "A6",
                               "A8",
                               "A10"});
    configureTable(objects_, {"№", "Имя", "Тип", "Свойство", "X, мм", "Y, мм", "Z, мм", "Rx, °",
                              "Ry, °", "Rz, °", "Ширина, мм", "Высота, мм", "Длина, мм", "R1, мм",
                              "R2, мм", "Материал", "Отражение", "Nx", "Ny"});
    configureTable(sources_, {"№", "Имя", "Форма", "Распределение", "X, мм", "Y, мм", "Z, мм",
                              "Rx, °", "Ry, °", "Rz, °", "Ширина, мм", "Высота, мм", "Конус, °",
                              "Мощность, Вт", "λ, мкм"});
    surfaces_->horizontalHeader()->moveSection(2, 1);
    surfaces_->horizontalHeader()->moveSection(surfaces_->horizontalHeader()->visualIndex(14), 6);
    surfaces_->horizontalHeader()->moveSection(surfaces_->horizontalHeader()->visualIndex(15), 7);
    surfaces_->horizontalHeader()->moveSection(surfaces_->horizontalHeader()->visualIndex(11), 10);
    surfaces_->horizontalHeaderItem(14)->setText("Покрытие T");
    surfaces_->horizontalHeaderItem(15)->setText("Покрытие R");
    surfaces_->setColumnWidth(0, 38);
    objects_->setColumnWidth(0, 38);
    sources_->setColumnWidth(0, 38);
    for (auto* table : {surfaces_, objects_, sources_}) {
        table->setColumnWidth(1, 160);
        table->verticalHeader()->setDefaultSectionSize(23);
    }
    for (int c : {3, 4, 14, 15})
        surfaces_->setColumnWidth(c, 80);
    surfaces_->setColumnWidth(2, 100);
    surfaces_->setColumnWidth(5, 90);
    surfaces_->setColumnWidth(6, 112);
    surfaces_->setColumnWidth(7, 85);
    surfaces_->setColumnWidth(2, 120);
    surfaces_->setColumnWidth(14, 110);
    surfaces_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(surfaces_, &QWidget::customContextMenuRequested, this, [this](QPoint point) {
        QMenu menu;
        const auto* item = surfaces_->itemAt(point);
        if (item && item->row() > 0 && item->row() <= int(project_.system.surfaces.size())) {
            const int row = item->row() - 1;
            for (int column : {3, 4}) {
                auto* action = menu.addAction(column == 3 ? "Связь радиуса…" : "Расчёт толщины…");
                connect(action, &QAction::triggered, this, [this, row, column] {
                    selectRow(row);
                    solveParameter(column);
                });
            }
            menu.addSeparator();
        }
        auto* advanced = menu.addAction("Расширенные параметры поверхностей");
        advanced->setCheckable(true);
        advanced->setChecked(advancedSurfaces_);
        connect(advanced, &QAction::toggled, this, [this](bool on) {
            advancedSurfaces_ = on;
            project_.workspace["advancedSurfaceColumns"] = on;
            rebuildEditors();
        });
        menu.exec(surfaces_->viewport()->mapToGlobal(point));
    });
    connect(surfaces_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (row > 0 && row <= int(project_.system.surfaces.size()) && (column == 3 || column == 4) &&
            isSolved(project_.system, column == 3 ? SolveParameter::Radius : SolveParameter::Thickness, row - 1)) {
            selectRow(row - 1);
            solveParameter(column);
            return;
        }
        if (!advancedSurfaces_ && row > 0 && row <= int(project_.system.surfaces.size()) &&
            (column == 14 || column == 11)) {
            selectRow(row - 1);
            properties(column);
        }
    });
    connect(surfaces_, &QTableWidget::cellChanged, this,
            [this](int r, int c) { editSurface(r, c); });
    connect(objects_, &QTableWidget::cellChanged, this, [this](int r, int c) { editObject(r, c); });
    connect(sources_, &QTableWidget::cellChanged, this, [this](int r, int c) { editSource(r, c); });
    connect(surfaces_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!building_ && row > 0 && row <= int(project_.system.surfaces.size()))
            selectRow(row - 1);
    });
    connect(objects_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!building_ && row >= 0)
            selectRow(row);
    });
    status_ = new QLabel;
    status_->setStyleSheet("color:white");
    statusBar()->addWidget(status_, 1);
    progress_ = new QProgressBar;
    progress_->setMaximumWidth(190);
    progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    viewMenu->addAction(navigator_->toggleViewAction());
    viewMenu->addAction(editorDock_->toggleViewAction());
    connect(modes_, &QTabBar::currentChanged, this, [this](int index) {
        if (building_)
            return;
        if (index < 2)
            changeMode(index);
        else
            buildRibbon();
    });
    debounce_.setSingleShot(true);
    debounce_.setInterval(120);
    connect(&debounce_, &QTimer::timeout, this, [this] { recalculate(); });
    connect(&watcher_, &QFutureWatcher<SceneTrace>::finished, this, [this] {
        progress_->hide();
        if (auto* trace = findChild<QToolButton*>("traceSceneButton"))
            trace->setText("Трасси-\nровка");
        try {
            auto result = watcher_.result();
            if (jobRevision_ == revision_) {
                results_->scene = std::move(result);
                results_->detectorStats.clear();
                for (const auto& dt : results_->scene->detectors) {
                    const auto& o = project_.scene.objects.at(dt.objectIndex);
                    results_->detectorStats.push_back(detectorStatistics(dt, o.size.x, o.size.y));
                }
                updatePlots();
                const auto& scene = project_.scene;
                const auto count =
                    std::count_if(scene.objects.begin(), scene.objects.end(),
                                  [](const auto& o) { return o.kind == ObjectKind::Detector; });
                status_->setText(
                    QString("Режим: непоследовательный    Объектов: %1    Источников: %2    "
                            "Детекторов: %3    Лучей: %4    Сегментов ≤ %5    %6")
                        .arg(scene.objects.size() - count)
                        .arg(scene.sources.size())
                        .arg(count)
                        .arg(results_->scene->launched)
                        .arg(scene.maxSegments)
                        .arg(results_->scene->cancelled ? "Расчёт остановлен" : "Расчёт завершён"));
                if (project_.mode == 1) {
                    int previousTab = views_->currentIndex();
                    addView(View::Detector);
                    views_->setCurrentIndex(previousTab);
                    detectorDock_->show();
                    rebuildTree();
                } else
                    recalculate();
            } else
                status_->setText("Сцена изменилась — запустите расчёт заново");
        } catch (const std::exception& e) {
            error(e);
        }
        sceneBusy_ = false;
    });
    try {
        optics::autofocus(project_.system, project_.catalog);
    } catch (...) {
    }
    changeMode(0);
    resizeDocks({navigator_}, {285}, Qt::Horizontal);
    resizeDocks({editorDock_}, {245}, Qt::Vertical);
    uiReady_ = true;
    QTimer::singleShot(0, this, [this] { fitRibbon(); });
}
Window::~Window() {
    cancel_->store(true);
    optimizationCancel_->store(true);
    watcher_.waitForFinished();
    optimizationWatcher_.waitForFinished();
}
void Window::error(const std::exception& e) {
    QMessageBox::warning(this, "Проверка данных", QString::fromUtf8(e.what()));
}
void Window::checkpoint() {
    undo_.push_back(serializeProject(project_));
    if (undo_.size() > 100)
        undo_.pop_front();
    redo_.clear();
}
void Window::changed(bool rebuild) {
    try { applySolves(project_.system, project_.catalog); }
    catch (const std::exception&) { /* recalculate reports an invalid constraint without stale analyses */ }
    if (!project_.system.solves.empty()) rebuild = true;
    dirty_ = true;
    ++revision_;
    if (results_) {
        results_->scene.reset();
        results_->detectorStats.clear();
    }
    if (rebuild) {
        rebuildEditors();
        rebuildTree();
    }
    debounce_.start();
    setWindowTitle("Optical CAD — Оптический модуль — " +
                   (path_.isEmpty()
                        ? str(project_.mode ? project_.scene.name : project_.system.name)
                        : QFileInfo(path_).fileName()) +
                   " *");
}
void Window::undo(bool redo) {
    auto& from = redo ? redo_ : undo_;
    auto& to = redo ? undo_ : redo_;
    if (from.empty())
        return;
    auto bytes = from.takeLast();
    to.push_back(serializeProject(project_));
    project_ = deserializeProject(bytes, false);
    changed();
    changeMode(project_.mode);
}
void Window::setProject(Project p) {
    applySolves(p.system, p.catalog);
    parkWorkspace();
    while (views_->count()) {
        auto* w = views_->widget(0);
        views_->removeTab(0);
        delete w;
    }
    project_ = std::move(p);
    advancedSurfaces_ = project_.workspace["advancedSurfaceColumns"].toBool(false);
    undo_.clear();
    redo_.clear();
    dirty_ = false;
    ++revision_;
    cancel_->store(true);
    optimizationCancel_->store(true);
    if (results_)
        results_->scene.reset();
    selected_ = 0;
    field_ = 0;
    changeMode(project_.mode);
    if (project_.workspace["docks"].isString())
        restoreState(QByteArray::fromBase64(project_.workspace["docks"].toString().toLatin1()));
    setWindowTitle("Optical CAD — Оптический модуль — " +
                   str(project_.mode ? project_.scene.name : project_.system.name));
}
void Window::changeMode(int mode) {
    if (building_ || mode < 0 || mode > 1)
        return;
    if (views_->count()) {
        QJsonArray tabs;
        for (int i = 0; i < views_->count(); ++i)
            if (auto* p = dynamic_cast<PlotWidget*>(views_->widget(i)))
                tabs.append(int(p->view));
        project_.workspace[project_.mode ? "nonsequentialViews" : "sequentialViews"] = tabs;
    }
    project_.mode = mode;
    building_ = true;
    modes_->setCurrentIndex(mode);
    building_ = false;
    selected_ = 0;
    hidden_.clear();
    parkWorkspace();
    while (views_->count()) {
        auto* w = views_->widget(0);
        views_->removeTab(0);
        delete w;
    }
    buildRibbon();
    rebuildEditors();
    rebuildTree();
    setWindowTitle("Optical CAD — Оптический модуль — " +
                   (path_.isEmpty() ? str(mode ? project_.scene.name : project_.system.name)
                                    : QFileInfo(path_).fileName()) +
                   (dirty_ ? " *" : ""));
    fieldBox_->setEnabled(mode == 0);
    detectorBox_->setEnabled(mode == 1);
    addView(mode ? View::Scene : View::Layout);
    editorPage_ = new QWidget;
    editorPage_->setObjectName("editorWorkspace");
    auto* pageLayout = new QVBoxLayout(editorPage_);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    int editorTab =
        views_->addTab(editorPage_, mode ? "Редактор объектов" : "Редактор поверхностей");
    views_->tabBar()->setTabButton(editorTab, QTabBar::RightSide, nullptr);
    if (mode) {
        editorDock_->hide();
        detectorDock_->show();
        resizeDocks({detectorDock_}, {250}, Qt::Vertical);
    } else {
        detectorDock_->hide();
        editorDock_->show();
    }
    for (auto* action : plotBar_->actions())
        if (auto* wa = qobject_cast<QWidgetAction*>(action)) {
            QWidget* w = wa->defaultWidget();
            if (w == fieldBox_ || w == scaleBox_ || w == scaleLabel_)
                action->setVisible(mode == 0);
            if (w == detectorBox_ || w == displayRays_ || w == displayLabel_)
                action->setVisible(mode == 1);
        }
    {
        QSignalBlocker block(displayRays_);
        displayRays_->setValue(project_.scene.displayRays);
    }
    recalculate();
    activateWorkspace();
    const auto saved =
        project_.workspace[mode ? "nonsequentialViews" : "sequentialViews"].toArray();
    for (auto v : saved) {
        int id = v.toInt(-1);
        if (id >= 0 && id <= 14 && ((mode == 1) == (id == 8 || id == 9 || id == 14)))
            addView(View(id));
    }
}
void Window::buildRibbon() {
    ribbon_->setProperty("naturalWidth", 0);
    ribbon_->setProperty("compressionSteps", 0);
    ribbon_->setProperty("availableWidth", 0);
    if (auto* old = ribbon_->layout()) {
        while (auto* item = old->takeAt(0)) {
            if (item->widget())
                delete item->widget();
            delete item;
        }
        delete old;
    }
    auto* row = new QHBoxLayout(ribbon_);
    row->setContentsMargins(6, 2, 4, 2);
    row->setSpacing(2);
    auto group = [&](QString title) {
        auto* g = new RibbonGroup(title);
        row->addWidget(g);
        return g;
    };
    auto unavailable = [](QToolButton* b, QString reason) {
        b->setEnabled(false);
        b->setToolTip("Ещё не реализовано: " + reason);
    };
    auto validate = [this] {
        auto errors = project_.mode ? project_.scene.validate(project_.catalog)
                                    : project_.system.validate(project_.catalog);
        QMessageBox::information(this, "Проверка",
                                 errors.empty()
                                     ? "Параметры и геометрия допустимы. Взаимные пересечения "
                                       "прозрачных тел требуют отдельной проверки."
                                     : str(errors.front()));
    };
    auto analysisIcon = [](View v) {
        switch (v) {
        case View::Layout:
            return "layout";
        case View::Spot:
            return "spot";
        case View::Fan:
            return "fan";
        case View::OPD:
            return "opd";
        case View::Wavefront:
            return "wavefront";
        case View::PSF:
            return "psf";
        case View::MTF:
            return "mtf";
        case View::RMS:
            return "rms";
        case View::Scene:
            return "scene";
        case View::Detector:
            return "map";
        default:
            return "wave";
        }
    };
    auto analysis = [&](RibbonGroup* g, QVBoxLayout* col, View v, QString label = {}) {
        return g->small(
            col, analysisIcon(v), label.isEmpty() ? viewName(v) : label,
            [this, v] {
                if ((v == View::Scene || v == View::Detector || v == View::DetectorProfile) != (project_.mode == 1))
                    changeMode(v == View::Scene || v == View::Detector || v == View::DetectorProfile ? 1 : 0);
                addView(v);
            },
            "analysis_" + QString::number(int(v)));
    };
    const int tab = modes_->currentIndex();
    if (tab == 2) {
        auto* g = group("Анализ системы");
        g->large("ray", "Одиночный\nлуч", [this] { rayReport(); });
        g->large("paraxial", "Параксиальная\nтрассировка", [this] { paraxialReport(); });
        g->large("layout", "Описание\nсистемы", [this] { prescription(); });
        if (!project_.mode) {
            g = group("Геометрический анализ");
            auto* col = g->column();
            for (auto v : {View::Spot, View::Fan, View::RMS})
                analysis(g, col, v);
            col = g->column();
            for (auto v : {View::Longitudinal, View::FieldCurvature, View::ChromaticFocus})
                analysis(g, col, v);
            g = group("Волновой анализ");
            col = g->column();
            for (auto v : {View::OPD, View::Wavefront, View::PSF})
                analysis(g, col, v);
            col = g->column();
            for (auto v : {View::MTF, View::GeometricMTF})
                analysis(g, col, v);
        } else {
            g = group("Сцена и приёмники");
            g->large("scene", "3D-модель\nхода лучей", [this] { addView(View::Scene); });
            g->large("map", "Карта\nоблучённости", [this] { addView(View::Detector); });
            auto* col = g->column();
            analysis(g, col, View::DetectorProfile, "Профили и статистика");
            g->small(col, "import", "Сводка детектора в CSV", [this] { exportDetectorStatistics(); }, "detectorStatisticsCSVButton");
        }
        g = group("Данные");
        g->large("import", "Выгрузка\nданных", [this] { exportCSV(); }, "csvButton");
        row->addStretch();
        return;
    }
    if (tab == 3) {
        auto* g = group("Оптимизация");
        auto* b =
            g->large("optimize", "Оптимизация\nрадиусов", [this] { optimize(); }, "optimizeButton");
        b->setEnabled(!project_.mode);
        b = g->large("focus", "Автофокус", [this] { autofocus(); }, "autofocusButton");
        b->setEnabled(!project_.mode);
        auto* col = g->column();
        b = g->small(col, "settings", "Редактор функции качества",
                     [this] { optimizationSettings(); }, "meritEditorButton");
        b->setEnabled(!project_.mode);
        b = g->small(col, "optimize", "Оптимизация по настройкам",
                     [this] { optimize(true); }, "customOptimizeButton");
        b->setEnabled(!project_.mode);
        g->small(col, "remove", "Остановить оптимизацию", [this] {
            optimizationCancel_->store(true);
        }, "cancelOptimizationButton");
        row->addStretch();
        return;
    }
    if (tab == 5) {
        auto* g = group("Оптические материалы");
        g->large("catalog", "Каталог\nстёкол", [this] { catalog(); });
        auto* col = g->column();
        g->small(col, "nd", "Задать вручную", [this] { manualMaterial(); });
        g->small(col, "import", "Импорт каталога", [this] { importCatalog(); });
        row->addStretch();
        return;
    }
    if (tab == 7) {
        auto* g = group("Окна рабочей области");
        g->large(project_.mode ? "scene" : "layout",
                 project_.mode ? "3D-модель\nхода лучей" : "Схема\nсистемы",
                 [this] { addView(project_.mode ? View::Scene : View::Layout); });
        g->large("editor", project_.mode ? "Редактор\nобъектов" : "Редактор\nповерхностей",
                 [this] { showEditor(); });
        auto* col = g->column();
        g->small(col, "map", "Показать нижнюю панель",
                 [this] { (project_.mode ? detectorDock_ : editorDock_)->show(); });
        g->small(col, "eye", "Показать все элементы", [this] {
            hidden_.clear();
            rebuildTree();
            updatePlots();
        });
        row->addStretch();
        return;
    }
    if (!project_.mode) {
        auto* g = group("Система");
        g->large("editor", "Параметры\nсистемы", [this] { parameters(); }, "parametersButton");
        auto* col = g->column();
        g->small(col, "aperture", "Апертура", [this] { parameters(); });
        g->small(col, "field", "Поля зрения", [this] { parameters(); });
        g->small(col, "wave", "Длины волн", [this] { parameters(); });
        col = g->column();
        unavailable(g->small(col, "polarization", "Поляризация", {}), "расчёт поляризации");
        unavailable(g->small(col, "thermo", "Среда", {}), "термооптика и атмосфера");
        g->small(col, "check", "Проверка", validate);
        g = group("Поверхности");
        g->large(
            "editor", "Редактор\nповерхностей", [this] { showEditor(); }, "surfaceEditorButton");
        col = g->column();
        g->small(col, "add", "Добавить", [this] { addSurface(); }, "addSurfaceButton");
        g->small(col, "remove", "Удалить", [this] { removeRow(); }, "deleteSurfaceButton");
        g->small(
            col, "stop", "Диафрагма",
            [this] {
                if (selected_ < 0 || selected_ >= int(project_.system.surfaces.size()))
                    return;
                checkpoint();
                project_.system.stop = selected_;
                changed();
            },
            "stopButton");
        col = g->column();
        g->small(col, "up", "Вверх", [this] { moveSurface(-1); });
        g->small(col, "down", "Вниз", [this] { moveSurface(1); });
        g->small(col, "tilt", "Наклон", [this] { properties(11); });
        g = group("Элементы");
        g->large("lens", "Линза", [this] { addLens(); }, "addLensButton");
        g->large("mirror", "Зеркало", [this] {
            if (!addSurface()) return;
            project_.system.surfaces[selected_].kind = SurfaceKind::Mirror;
            changed();
        });
        unavailable(g->large("prism", "Призма", {}), "призма из произвольных граней");
        g->large("ray", "Объект", [this] { parameters(); });
        g->large("image", "Плоскость\nизобр.", [this] { parameters(); });
        g->large("focus", "Автофокус", [this] { autofocus(); }, "autofocusButton");
        g = group("Материалы");
        g->large("catalog", "Каталог\nстёкол", [this] { catalog(); });
        col = g->column();
        g->small(col, "nd", "Задать вручную", [this] { manualMaterial(); });
        g->small(col, "import", "Импорт каталога", [this] { importCatalog(); });
        g = group("Расчёт");
        g->large("trace", "Трасси-\nровка", [this] { recalculate(); }, "traceButton");
        col = g->column();
        g->small(col, "paraxial", "Параксиальная", [this] { paraxialReport(); });
        g->small(col, "rays", "Число лучей", [this] { parameters(); });
        g->small(col, "optimize", "Оптимизация", [this] { optimize(); }, "optimizeButton");
        g = group("Анализ");
        g->large("layout", "Схема\nсистемы", [this] { addView(View::Layout); }, "analysis_0");
        g->large("spot", "Точечная\nдиаграмма", [this] { addView(View::Spot); }, "analysis_1");
        col = g->column();
        analysis(g, col, View::MTF);
        analysis(g, col, View::Fan, "Аберрации");
        analysis(g, col, View::OPD);
        col = g->column();
        analysis(g, col, View::Wavefront);
        analysis(g, col, View::PSF);
        analysis(g, col, View::RMS);
        g = group("Обмен");
        col = g->column();
        unavailable(g->small(col, "step", "Экспорт STEP", {}), "экспорт CAD геометрии");
        g->small(col, "import", "Выгрузка данных", [this] { exportCSV(); }, "csvButton");
        g->small(col, "focus", "Дефокус", [this] { parameters(); });
        // Extended analyses are on the dedicated Analysis context tab.
    } else {
        auto* g = group("Система");
        g->large("editor", "Редактор\nобъектов", [this] { showEditor(); }, "objectEditorButton");
        auto* col = g->column();
        g->small(
            col, "editor", "Параметры расчёта", [this] { calculationParameters(); },
            "sceneParametersButton");
        g->small(col, "fan", "Предел сегментов", [this] { calculationParameters(); });
        unavailable(g->small(col, "polarization", "Поляризация", {}), "расчёт поляризации");
        g = group("Объекты");
        g->large("lens", "Линза", [this] { addObject(ObjectKind::Lens); }, "object_0");
        g->large("mirror", "Зеркало", [this] { addObject(ObjectKind::Mirror); }, "object_1");
        auto* prism =
            g->large("prism", "Призма", [this] { addObject(ObjectKind::Prism); }, "object_6");
        prism->setToolTip("Замкнутая треугольная призма: ширина X, высота Y, глубина Z; угол "
                          "вершины 2 atan(X / (2 Z))");
        g->large("box", "Тело", [this] { addObject(ObjectKind::Box); }, "object_3");
        g->large("sphere", "Сфера", [this] { addObject(ObjectKind::Sphere); }, "object_2");
        g->large("cylinder", "Цилиндр", [this] { addObject(ObjectKind::Cylinder); }, "object_4");
        unavailable(g->large("cad", "Импорт\nCAD", {}), "STEP/IGES");
        g = group("Источники");
        g->large("source", "Точечный", [this] { addSource(SourceShape::Point); }, "source_0");
        col = g->column();
        g->small(
            col, "rectangleSource", "Прямоугольный", [this] { addSource(SourceShape::Rectangle); },
            "source_1");
        g->small(
            col, "ellipseSource", "Эллиптический", [this] { addSource(SourceShape::Ellipse); },
            "source_2");
        unavailable(g->small(col, "import", "Из файла", {}), "измеренные диаграммы источников");
        col = g->column();
        g->small(col, "source", "Распределение", [this] { sourceProperties(3); });
        g->small(col, "source", "Мощность", [this] { sourceProperties(13); });
        g->small(col, "rays", "Число лучей", [this] { calculationParameters(); });
        g = group("Приёмники");
        g->large(
            "detector", "Детектор\nплоский", [this] { addObject(ObjectKind::Detector); },
            "addDetectorButton");
        col = g->column();
        unavailable(g->small(col, "material", "Объёмный", {}), "объёмные детекторы");
        unavailable(g->small(col, "ellipseSource", "Поверхностный", {}),
                    "детектор на произвольной поверхности");
        unavailable(g->small(col, "axes", "Сист. координат", {}),
                    "иерархические системы координат");
        g = group("Свойства");
        col = g->column();
        g->small(col, "axes", "Положение", [this] { properties(4); });
        g->small(col, "tilt", "Наклон", [this] { properties(7); });
        g->small(col, "material", "Материал", [this] { catalog(); });
        col = g->column();
        g->small(col, "coating", "Покрытие", [this] { properties(16); });
        g->small(col, "scatter", "Рассеяние", [this] { properties(3); });
        g->small(col, "check", "Проверка тел", validate);
        g = group("Расчёт и анализ");
        g->large(
            "trace", sceneBusy_ ? "Остано-\nвить" : "Трасси-\nровка",
            [this] {
                if (sceneBusy_)
                    cancel_->store(true);
                else
                    runScene();
            },
            "traceSceneButton");
        g->large("scene", "3D-модель\nхода лучей", [this] { addView(View::Scene); }, "analysis_8");
        g->large("map", "Карта\nоблуч.", [this] { addView(View::Detector); }, "analysis_9");
        col = g->column();
        g->small(col, "mtf", "Шкала лог/лин", [this] {
            for (auto* p : findChildren<PlotWidget*>())
                if (p->view == View::Detector) {
                    p->logarithmic = !p->logarithmic;
                    p->update();
                }
        });
        g->small(col, "import", "Выгрузка данных", [this] { exportCSV(); }, "csvButton");
        unavailable(g->small(col, "split", "Смешанный режим", {}),
                    "совместная трассировка двух библиотек");
        g = group("Обмен");
        col = g->column();
        unavailable(g->small(col, "step", "Экспорт STEP", {}), "экспорт CAD геометрии");
        g->small(col, "import", "Импорт каталога", [this] { importCatalog(); });
    }
    row->addStretch();
    ribbon_->setProperty("naturalWidth", row->sizeHint().width());
    QTimer::singleShot(0, this, [this] { fitRibbon(); });
}

void Window::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (uiReady_)
        QTimer::singleShot(0, this, [this] { fitRibbon(); });
}
void Window::fitRibbon() {
    auto* scroll = findChild<QScrollArea*>("ribbonScroll");
    if (!scroll || !ribbon_->layout())
        return;
    int natural = ribbon_->property("naturalWidth").toInt();
    if (natural <= 0)
        natural = ribbon_->layout()->sizeHint().width();
    natural = std::max(natural, ribbon_->layout()->minimumSize().width());
    ribbon_->setProperty("naturalWidth", natural);
    const int available = scroll->viewport()->width() - 8;
    if (ribbon_->property("availableWidth").toInt() != available) {
        ribbon_->setProperty("compressionSteps", 0);
        ribbon_->setProperty("availableWidth", available);
    }
    const int steps = ribbon_->property("compressionSteps").toInt();
    double scale = std::min(1., double(available) / std::max(1, natural));
    for (auto* b : ribbon_->findChildren<QToolButton*>()) {
        if (!b->property("ribbonCommand").toBool())
            continue;
        b->setStyleSheet(
            QString("font-size:%1px").arg(std::max(7, int(std::floor(12 * scale)) - steps)));
        bool large = b->property("largeCommand").toBool();
        int size = large ? std::max(18, qRound(32 * scale) - 2 * steps)
                         : std::max(10, qRound(16 * scale) - steps);
        b->setIconSize({size, size});
        b->updateGeometry();
    }
    // Each group and command column caches its minimum size. Refresh nested
    // layouts too, so the scroll area can shrink after changing font metrics.
    for (auto* layout : ribbon_->findChildren<QLayout*>())
        layout->invalidate();
    ribbon_->layout()->activate();
    ribbon_->updateGeometry();
    QTimer::singleShot(0, this, [this] {
        auto* scroll = findChild<QScrollArea*>("ribbonScroll");
        const int steps = ribbon_->property("compressionSteps").toInt();
        if (scroll && scroll->horizontalScrollBar()->maximum() > 0 && steps < 6) {
            ribbon_->setProperty("compressionSteps", steps + 1);
            fitRibbon();
        }
    });
}

void Window::parkWorkspace() {
    // These widgets are shared between pages, so they must survive page replacement.
    plotBar_->setParent(this);
    plotBar_->hide();
    editors_->setParent(editorDock_);
    editorDock_->setWidget(editors_);
    editorPage_ = nullptr;
}
void Window::activateWorkspace() {
    if (!editorDock_ || !editorPage_)
        return;
    if (views_->currentWidget() == editorPage_) {
        plotBar_->hide();
        plotBar_->setParent(this);
        editorDock_->setWidget(nullptr);
        editors_->setParent(editorPage_);
        editorPage_->layout()->addWidget(editors_);
        editors_->show();
        editorDock_->hide();
    } else {
        editors_->setParent(editorDock_);
        editorDock_->setWidget(editors_);
        editorDock_->setVisible(project_.mode == 0);
        if (auto* plot = dynamic_cast<PlotWidget*>(views_->currentWidget())) {
            plotBar_->setParent(plot);
            plotBar_->setGeometry(0, 0, plot->width(), 29);
            plotBar_->show();
            plotBar_->raise();
            QSignalBlocker blocker(scaleBox_);
            scaleBox_->setValue(plot->zoom);
        }
    }
}
void Window::showEditor() {
    if (editorPage_)
        views_->setCurrentWidget(editorPage_);
}
void Window::filterTree() {
    const QString needle = treeSearch_->text().trimmed();
    std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* item) {
        bool found = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive) ||
                     item->text(1).contains(needle, Qt::CaseInsensitive);
        for (int i = 0; i < item->childCount(); ++i)
            found = apply(item->child(i)) || found;
        item->setHidden(!found);
        if (found && !needle.isEmpty())
            item->setExpanded(true);
        return found;
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        apply(tree_->topLevelItem(i));
}
void Window::properties(int column) {
    auto* table = project_.mode ? objects_ : surfaces_;
    if (selected_ < 0 || selected_ >= table->rowCount())
        return;
    const int row = project_.mode ? selected_ : selected_ + 1;
    showEditor();
    table->setCurrentCell(row, column);
    table->scrollToItem(table->item(row, column));
    if (!project_.mode && (column == 14 || column == 11)) {
        const auto surface = project_.system.surfaces.at(selected_);
        QDialog dialog(this);
        dialog.setObjectName("surfacePropertyDialog");
        dialog.setWindowTitle(column == 14 ? "Покрытие — коэффициенты поверхности"
                                           : "Наклон и децентрировка");
        auto* form = new QFormLayout(&dialog);
        QList<QDoubleSpinBox*> values;
        const QStringList labels =
            column == 14
                ? QStringList{"Пропускание T", "Отражение R (зеркало)"}
                : QStringList{"Наклон X, °",         "Наклон Y, °",         "Наклон Z, °",
                              "Децентрировка X, мм", "Децентрировка Y, мм", "Смещение Z, мм"};
        const std::vector<double> initial =
            column == 14
                ? std::vector<double>{surface.transmission, surface.reflectivity}
                : std::vector<double>{surface.tilt.x,     surface.tilt.y,     surface.tilt.z,
                                      surface.decenter.x, surface.decenter.y, surface.decenter.z};
        for (int i = 0; i < labels.size(); ++i) {
            auto* value = spin(initial[i], column == 14 ? 0 : -1e6, column == 14 ? 1 : 1e6, 6);
            value->setObjectName(QString("surfaceProperty_%1").arg(i));
            values << value;
            form->addRow(labels[i], value);
        }
        if (column == 14)
            form->addRow(new QLabel("T применяется к пропусканию по Френелю. Спектральные покрытия "
                                    "ещё не моделируются."));
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            auto candidate = project_.system;
            auto& updated = candidate.surfaces.at(selected_);
            if (column == 14) {
                updated.transmission = values[0]->value();
                updated.reflectivity = values[1]->value();
            } else {
                updated.tilt = {values[0]->value(), values[1]->value(), values[2]->value()};
                updated.decenter = {values[3]->value(), values[4]->value(), values[5]->value()};
            }
            try { applySolves(candidate, project_.catalog); }
            catch (const std::exception& e) { error(e); return; }
            checkpoint();
            project_.system = std::move(candidate);
            changed();
        }
        return;
    }
    if (column == 3 && project_.mode) {
        bool ok = false;
        QString value =
            QInputDialog::getItem(this, "Поведение поверхности", "Взаимодействие", interactions,
                                  int(project_.scene.objects[selected_].interaction), false, &ok);
        if (ok)
            qobject_cast<QComboBox*>(table->cellWidget(row, 3))->setCurrentText(value);
    } else {
        const int count = (column == 4 || column == 7 || (!project_.mode && column == 11)) ? 3 : 1;
        QDialog dialog(this);
        dialog.setWindowTitle(column == 16  ? "Коэффициент отражения"
                              : column == 4 ? "Положение объекта"
                                            : "Наклон элемента");
        auto* form = new QFormLayout(&dialog);
        QList<QDoubleSpinBox*> values;
        for (int i = 0; i < count; ++i) {
            auto* value = spin(table->item(row, column + i)->text().toDouble(),
                               column == 16 ? 0 : -1e6, column == 16 ? 1 : 1e6, 6);
            form->addRow(table->horizontalHeaderItem(column + i)->text(), value);
            values << value;
        }
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            for (int i = 0; i < count; ++i)
                table->item(row, column + i)->setText(num(values[i]->value()));
    }
}
void Window::sourceProperties(int column) {
    int row = std::max(0, sources_->currentRow());
    if (row >= sources_->rowCount())
        return;
    if (column == 3) {
        bool ok = false;
        auto value =
            QInputDialog::getItem(this, "Источник", "Угловое распределение", distributions,
                                  int(project_.scene.sources[row].distribution), false, &ok);
        if (ok)
            qobject_cast<QComboBox*>(sources_->cellWidget(row, column))->setCurrentText(value);
    } else {
        bool ok = false;
        double value = QInputDialog::getDouble(this, "Источник", "Мощность, Вт",
                                               project_.scene.sources[row].power, 0, 1e6, 6, &ok);
        if (ok)
            sources_->item(row, column)->setText(num(value));
    }
}
void Window::rebuildEditors() {
    building_ = true;
    // A model row can become a read-only endpoint after insertion/removal.
    // Remove its old combo widgets as well as the old item values.
    for (auto* table : {surfaces_, objects_, sources_}) {
        for (int r = 0; r < table->rowCount(); ++r)
            for (int c = 0; c < table->columnCount(); ++c)
                if (auto* widget = table->cellWidget(r, c)) {
                    widget->hide();
                    table->removeCellWidget(r, c);
                }
        table->clearContents();
    }
    auto set = [&](QTableWidget* t, int r, int c, QString value, bool editable = true) {
        auto* item = new QTableWidgetItem(value);
        if (!editable)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        t->setItem(r, c, item);
    };
    auto combo = [&](QTableWidget* t, int r, int c, QStringList values, int selected,
                     std::function<void(int)> fn) {
        auto* box = new QComboBox;
        box->addItems(values);
        box->setCurrentIndex(selected);
        t->setCellWidget(r, c, box);
        connect(box, &QComboBox::currentIndexChanged, this, [this, fn](int i) {
            if (!building_ && i >= 0)
                fn(i);
        });
    };
    auto glassCombo = [&](QTableWidget* t, int r, int c, const std::string& current,
                          std::function<void(std::string)> fn) {
        QStringList names;
        for (auto m : project_.catalog.materials)
            names << str(m.name);
        combo(t, r, c, names, names.indexOf(str(current)),
              [names, fn](int i) { fn(names[i].toStdString()); });
    };
    auto& s = project_.system;
    surfaces_->horizontalHeaderItem(2)->setText("Тип поверхности");
    surfaces_->horizontalHeaderItem(14)->setText(advancedSurfaces_ ? "Покрытие T" : "Покрытие");
    surfaces_->horizontalHeaderItem(11)->setText(advancedSurfaces_ ? "Rx, °" : "Наклон");
    for (int c : {8, 9, 10, 12, 13, 15, 16, 17, 18, 19})
        surfaces_->setColumnHidden(c, !advancedSurfaces_);
    surfaces_->setRowCount(s.surfaces.size() + 2);
    for (int row : {0, int(s.surfaces.size() + 1)})
        for (int col = 0; col < surfaces_->columnCount(); ++col)
            set(surfaces_, row, col, "", false);
    set(surfaces_, 0, 0, "0", false);
    set(surfaces_, 0, 1, "Объект", false);
    set(surfaces_, 0, 2, "Объект", false);
    set(surfaces_, 0, 3, "∞", false);
    set(surfaces_, 0, 4, s.objectDistance > 0 ? num(s.objectDistance) : QString("∞"), false);
    const int imageRow = s.surfaces.size() + 1;
    set(surfaces_, imageRow, 0, QString::number(imageRow), false);
    set(surfaces_, imageRow, 1, "Изображение", false);
    set(surfaces_, imageRow, 2, "Изображение", false);
    set(surfaces_, imageRow, 3, "∞", false);
    set(surfaces_, imageRow, 5, str(s.surfaces.back().material), false);
    for (int r = 0; r < int(s.surfaces.size()); ++r) {
        auto& v = s.surfaces[r];
        set(surfaces_, r + 1, 0, QString::number(r + 1) + (size_t(r) == s.stop ? " ●" : ""), false);
        set(surfaces_, r + 1, 1, str(v.name));
        combo(surfaces_, r + 1, 2, surfaceKinds, int(v.kind), [this, r](int i) {
            checkpoint();
            project_.system.surfaces[r].kind = SurfaceKind(i);
            changed();
        });
        set(surfaces_, r + 1, 3, num(v.radius));
        set(surfaces_, r + 1, 4, num(v.thickness));
        for (const auto& a : s.solves)
            if (a.surface == size_t(r)) {
                auto* item = surfaces_->item(r + 1, a.parameter == SolveParameter::Radius ? 3 : 4);
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setIcon(officeIcon("link"));
                item->setForeground(QColor("#245f99"));
                item->setToolTip(solveDescription(a) + "\nДвойной щелчок — изменить связь");
            }
        glassCombo(surfaces_, r + 1, 5, v.material, [this, r](std::string m) {
            checkpoint();
            project_.system.surfaces[r].material = m;
            changed();
        });
        const double values[] = {v.semiDiameter, v.conic,        v.decenter.x, v.decenter.y,
                                 v.decenter.z,   v.tilt.x,       v.tilt.y,     v.tilt.z,
                                 v.transmission, v.reflectivity, v.asphere[0], v.asphere[1],
                                 v.asphere[2],   v.asphere[3]};
        for (int c = 6; c < 20; ++c)
            set(surfaces_, r + 1, c, num(values[c - 6]));
        if (!advancedSurfaces_) {
            set(surfaces_, r + 1, 14,
                v.transmission == 1 && v.reflectivity == 1
                    ? QString("—")
                    : QString("T %1 / R %2").arg(num(v.transmission), num(v.reflectivity)),
                false);
            set(surfaces_, r + 1, 11,
                v.tilt.norm2() + v.decenter.norm2() < 1e-20
                    ? QString("—")
                    : QString("%1; %2; %3 °").arg(num(v.tilt.x), num(v.tilt.y), num(v.tilt.z)),
                false);
            surfaces_->item(r + 1, 14)->setToolTip("Двойной щелчок — коэффициенты T/R");
            surfaces_->item(r + 1, 11)->setToolTip("Двойной щелчок — наклон и децентрировка");
        }
        if (size_t(r) == s.stop)
            for (int c = 0; c < 20; ++c)
                if (surfaces_->item(r + 1, c))
                    surfaces_->item(r + 1, c)->setBackground(QColor("#d7e9f8"));
    }
    auto& scene = project_.scene;
    objects_->setRowCount(scene.objects.size());
    for (int r = 0; r < int(scene.objects.size()); ++r) {
        auto& o = scene.objects[r];
        set(objects_, r, 0, QString::number(r + 1), false);
        set(objects_, r, 1, str(o.name));
        combo(objects_, r, 2, objectKinds, int(o.kind), [this, r](int i) {
            checkpoint();
            project_.scene.objects[r].kind = ObjectKind(i);
            changed();
        });
        combo(objects_, r, 3, interactions, int(o.interaction), [this, r](int i) {
            checkpoint();
            project_.scene.objects[r].interaction = Interaction(i);
            changed();
        });
        double values[] = {o.pose.position.x, o.pose.position.y, o.pose.position.z, o.pose.tilt.x,
                           o.pose.tilt.y,     o.pose.tilt.z,     o.size.x,          o.size.y,
                           o.size.z,          o.radius1,         o.radius2};
        for (int c = 4; c < 15; ++c)
            set(objects_, r, c, num(values[c - 4]));
        glassCombo(objects_, r, 15, o.material, [this, r](std::string m) {
            checkpoint();
            project_.scene.objects[r].material = m;
            changed();
        });
        set(objects_, r, 16, num(o.reflectivity));
        set(objects_, r, 17, QString::number(o.nx));
        set(objects_, r, 18, QString::number(o.ny));
    }
    sources_->setRowCount(scene.sources.size());
    for (int r = 0; r < int(scene.sources.size()); ++r) {
        auto& s = scene.sources[r];
        set(sources_, r, 0, QString::number(r + 1), false);
        set(sources_, r, 1, str(s.name));
        combo(sources_, r, 2, shapes, int(s.shape), [this, r](int i) {
            checkpoint();
            project_.scene.sources[r].shape = SourceShape(i);
            changed();
        });
        combo(sources_, r, 3, distributions, int(s.distribution), [this, r](int i) {
            checkpoint();
            project_.scene.sources[r].distribution = Distribution(i);
            changed();
        });
        double values[] = {s.pose.position.x, s.pose.position.y, s.pose.position.z, s.pose.tilt.x,
                           s.pose.tilt.y,     s.pose.tilt.z,     s.width,           s.height,
                           s.coneAngle,       s.power,           s.wavelength};
        for (int c = 4; c < 15; ++c)
            set(sources_, r, c, num(values[c - 4]));
    }
    while (editors_->count())
        editors_->removeTab(0);
    if (project_.mode == 0)
        editors_->addTab(surfaces_, "Поверхности");
    else {
        editors_->addTab(objects_, "Редактор объектов");
        editors_->addTab(sources_, "Редактор источников");
    }
    editors_->tabBar()->setVisible(project_.mode != 0);
    fieldBox_->clear();
    for (auto field : s.fields)
        fieldBox_->addItem(QString("%1° / %2°").arg(field.x).arg(field.y));
    field_ = std::clamp(field_, 0, int(s.fields.size()) - 1);
    fieldBox_->setCurrentIndex(field_);
    int detector = detectorBox_->currentIndex();
    detectorBox_->clear();
    for (auto& o : scene.objects)
        if (o.kind == ObjectKind::Detector)
            detectorBox_->addItem(str(o.name));
    detectorBox_->setCurrentIndex(std::max(0, detector));
    building_ = false;
}
void Window::rebuildTree() {
    building_ = true;
    QSet<QString> expanded;
    QTreeWidgetItemIterator before(tree_);
    while (*before) {
        if ((*before)->isExpanded())
            expanded.insert((*before)->text(0));
        ++before;
    }
    tree_->clear();
    auto node = [&](QTreeWidgetItem* parent, QString name, QString value = QString(),
                    QString icon = QString("folder")) {
        auto* item = new QTreeWidgetItem(parent, {name, value, "", ""});
        item->setIcon(0, officeIcon(icon));
        item->setIcon(2, officeIcon("eye"));
        item->setIcon(3, officeIcon("circle"));
        item->setToolTip(0, name);
        item->setToolTip(1, value);
        item->setToolTip(2, "Видимость в окне схемы; расчёт не меняется");
        item->setToolTip(3, "Выбранный элемент");
        return item;
    };
    auto vector = [](Vec3 v, QString unit) {
        return QString("%1; %2; %3 %4").arg(num(v.x), num(v.y), num(v.z), unit);
    };
    auto* root = new QTreeWidgetItem(
        tree_, {str(project_.mode ? project_.scene.name : project_.system.name), "", "", ""});
    root->setIcon(0, officeIcon("editor"));
    auto font = root->font(0);
    font.setBold(true);
    root->setFont(0, font);
    if (!project_.mode) {
        auto& s = project_.system;
        auto* params = node(root, "Параметры системы");
        params->setData(0, Qt::UserRole + 1, "parameters");
        node(params, "Апертура", num(s.pupilDiameter) + " мм", "aperture");
        node(params, "Поля зрения", QString::number(s.fields.size()) + " поля", "field");
        node(params, "Длины волн", QString::number(s.wavelengths.size()) + " волны", "wave");
        node(params, "Поляризация", "не учитывается", "polarization");
        node(params, "Среда", "опорные условия", "thermo");
        auto* surfaces = node(root, "Поверхности", QString::number(s.surfaces.size() + 2) + " шт.");
        node(surfaces, "0 Объект", s.objectDistance > 0 ? num(s.objectDistance) + " мм" : "∞",
             "circle");
        for (size_t i = 0; i < s.surfaces.size(); ++i) {
            auto& v = s.surfaces[i];
            auto* item =
                node(surfaces,
                     QString::number(i + 1) + " " + str(v.name) + (i == s.stop ? " (STOP)" : ""),
                     "R " + (v.radius == 0 ? QString("∞") : num(v.radius)),
                     i == s.stop                     ? "stop"
                     : v.kind == SurfaceKind::Mirror ? "mirror"
                                                     : "lens");
            item->setData(0, Qt::UserRole, int(i));
            node(item, "Радиус кривизны", v.radius == 0 ? QString("∞") : num(v.radius) + " мм",
                 "circle");
            node(item, "Толщина", num(v.thickness) + " мм", "editor");
            node(item, "Материал", str(v.material), "material");
            node(item, "Свет. полудиаметр", num(v.semiDiameter) + " мм", "aperture");
            node(item, "Покрытие",
                 QString("T %1; R %2").arg(num(v.transmission), num(v.reflectivity)), "coating");
            node(item, "Наклон / децентр.", vector(v.tilt, "°") + " / " + vector(v.decenter, "мм"),
                 "tilt");
            if (hidden_.contains(int(i)))
                item->setIcon(2, officeIcon("hidden"));
            if (int(i) == selected_) {
                tree_->setCurrentItem(item);
                item->setIcon(3, officeIcon("field"));
            }
        }
        QString imagePosition;
        try { imagePosition = "Z " + num(s.imageZ(project_.catalog)) + " мм"; }
        catch (const std::exception&) { imagePosition = "Ошибка связи параметров"; }
        node(surfaces, QString::number(s.surfaces.size() + 1) + " Изображение", imagePosition, "image");
        auto* rays = node(root, "Лучи", QString::number(s.fields.size()) + " пучка");
        for (size_t i = 0; i < s.fields.size(); ++i) {
            auto* field =
                node(rays, QString("Пучок поля %1°").arg(s.fields[i].y), "13 лучей", "fan");
            node(field, "Тип сетки", "меридиональный срез", "grid");
            node(field, "Число лучей", "13", "rays");
            node(field, "Цвет отображения",
                 i == 0   ? "синий"
                 : i == 1 ? "зелёный"
                          : "красный",
                 "wave");
        }
    } else {
        auto& s = project_.scene;
        auto* params = node(root, "Параметры расчёта");
        params->setData(0, Qt::UserRole + 1, "parameters");
        node(params, "Лучей на анализ", QString::number(s.rayCount), "editor");
        node(params, "Предел сегментов", QString::number(s.maxSegments), "editor");
        node(params, "Учёт поляризации", "не учитывается", "polarization");
        auto* coordinates = node(root, "Системы координат", "глобальная");
        node(coordinates, "Начало X/Y/Z", "0; 0; 0 мм", "axes");
        auto* srcs = node(root, "Источники", QString::number(s.sources.size()) + " шт.");
        for (size_t i = 0; i < s.sources.size(); ++i) {
            auto& src = s.sources[i];
            auto* source = node(srcs, str(src.name), num(src.power) + " Вт", "source");
            source->setData(0, Qt::UserRole + 2, int(i));
            node(source, "Положение X/Y/Z", vector(src.pose.position, "мм"), "axes");
            node(source, "Наклон X/Y/Z", vector(src.pose.tilt, "°"), "tilt");
            node(source, "Распределение", distributions[int(src.distribution)], "source");
            node(source, "Мощность", num(src.power) + " Вт", "source");
            node(source, "Лучей на схеме", QString::number(s.displayRays), "rays");
        }
        size_t count = std::count_if(s.objects.begin(), s.objects.end(),
                                     [](const auto& o) { return o.kind == ObjectKind::Detector; });
        auto* objects = node(root, "Объекты", QString::number(s.objects.size() - count) + " шт.");
        auto* detectors = node(root, "Детекторы", QString::number(count) + " шт.");
        const QStringList icons = {"lens",     "mirror",   "sphere", "box",
                                   "cylinder", "detector", "prism"};
        for (size_t i = 0; i < s.objects.size(); ++i) {
            auto& o = s.objects[i];
            bool detector = o.kind == ObjectKind::Detector;
            auto* item =
                node(detector ? detectors : objects, str(o.name),
                     detector ? QString("%1×%2").arg(o.nx).arg(o.ny)
                     : o.interaction == Interaction::Dielectric ? str(o.material)
                                                                : interactions[int(o.interaction)],
                     icons[int(o.kind)]);
            item->setData(0, Qt::UserRole, int(i));
            if (detector) {
                node(item, "Размер", num(o.size.x) + " × " + num(o.size.y) + " мм", "editor");
                node(item, "Ячеек сетки", QString("%1 × %2").arg(o.nx).arg(o.ny), "grid");
                node(item, "Регистрируется", "облучённость, Вт/мм²", "map");
            } else {
                node(item, "Положение X/Y/Z", vector(o.pose.position, "мм"), "axes");
                node(item, "Наклон X/Y/Z", vector(o.pose.tilt, "°"), "tilt");
                node(item, "Материал", str(o.material), "material");
                if (o.kind == ObjectKind::Prism)
                    node(item, "Угол вершины", num(2 * atan(o.size.x / (2 * o.size.z)) / deg) + "°",
                         "prism");
                node(item, "Покрытие", "R " + num(o.reflectivity), "coating");
                node(item, "Рассеяние",
                     o.interaction == Interaction::Diffuse ? "ламбертовское" : "не задано",
                     "scatter");
            }
            if (hidden_.contains(int(i)))
                item->setIcon(2, officeIcon("hidden"));
            if (int(i) == selected_) {
                tree_->setCurrentItem(item);
                item->setIcon(3, officeIcon("field"));
            }
        }
    }
    auto* analysis = node(root, "Анализ", QString::number(views_->count() - 1) + " окна");
    for (int i = 0; i < views_->count(); ++i)
        if (auto* plot = dynamic_cast<PlotWidget*>(views_->widget(i))) {
            auto* item = node(analysis, viewName(plot->view), {},
                              plot->view == View::Detector ? "map"
                              : plot->view == View::Scene  ? "scene"
                                                           : "mtf");
            item->setData(0, Qt::UserRole + 4, int(plot->view));
        }
    node(root, "Материалы", QString::number(project_.catalog.materials.size()) + " записей",
         "folder");
    tree_->expandToDepth(1);
    if (project_.mode) {
        auto* src = root->child(2);
        src->setExpanded(true);
        if (src->childCount())
            src->child(0)->setExpanded(true);
    }
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
        if (expanded.contains((*it)->text(0)))
            (*it)->setExpanded(true);
        ++it;
    }
    building_ = false;
    filterTree();
}
void Window::selectRow(int r) {
    selected_ = r;
    building_ = true;
    auto* t = project_.mode ? objects_ : surfaces_;
    if (r >= 0 && r < t->rowCount())
        t->selectRow(project_.mode ? r : r + 1);
    QTreeWidgetItemIterator it(tree_);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).isValid() && (*it)->data(0, Qt::UserRole).toInt() == r) {
            tree_->setCurrentItem(*it);
            (*it)->setIcon(3, officeIcon("field"));
        }
        ++it;
    }
    building_ = false;
    if (results_) {
        results_->selected = r;
        updatePlots();
    }
}
void Window::addView(View v) {
    for (int i = 0; i < views_->count(); ++i)
        if (auto* p = dynamic_cast<PlotWidget*>(views_->widget(i)); p && p->view == v) {
            views_->setCurrentIndex(i);
            return;
        }
    auto* plot = new PlotWidget(v);
    plot->select = [this](int i) { selectRow(i); };
    plot->setData(results_);
    int i = views_->addTab(plot, viewName(v));
    auto* close = new QToolButton;
    close->setText("×");
    close->setFixedSize(14, 14);
    close->setStyleSheet("border:0;background:transparent;color:#8394a3;padding:0");
    views_->tabBar()->setTabButton(i, QTabBar::RightSide, close);
    connect(close, &QToolButton::clicked, this, [this, plot] {
        int id = views_->indexOf(plot);
        if (id >= 0)
            emit views_->tabCloseRequested(id);
    });
    plot->officePresentation = true;
    views_->setCurrentIndex(i);
}
void Window::updatePlots() {
    if (results_) {
        results_->hidden = hidden_;
        results_->displayRays = displayRays_->value();
    }
    for (auto* plot : findChildren<PlotWidget*>())
        plot->setData(results_);
    if (results_ && results_->scene && !results_->scene->detectors.empty()) {
        const auto& dt = results_->scene->detectors[std::min(
            size_t(results_->detector), results_->scene->detectors.size() - 1)];
        detectorDock_->setWindowTitle("Карта облучённости — " +
                                      str(project_.scene.objects[dt.objectIndex].name));
    }
}
void Window::recalculate() {
    auto previous = results_;
    results_ = std::make_shared<Results>();
    results_->project = project_;
    results_->field = field_;
    results_->selected = selected_;
    results_->detector = std::max(0, detectorBox_->currentIndex());
    if (previous && previous->scene) {
        results_->scene = previous->scene;
        results_->detectorStats = previous->detectorStats;
    }
    if (project_.mode == 0) {
        auto errors = project_.system.validate(project_.catalog);
        if (!errors.empty())
            results_->error = "Исправьте данные: " + str(errors.front());
        else
            try {
                for (auto f : project_.system.fields)
                    results_->spots.push_back(spot(project_.system, project_.catalog, f));
                auto f = project_.system.fields.at(field_);
                try {
                    results_->fan = rayFan(project_.system, project_.catalog, f);
                } catch (const std::exception& e) {
                    results_->fanError = str(e.what());
                }
                try {
                    results_->wave = wavefront(project_.system, project_.catalog, f);
                    results_->diffraction = diffraction(project_.system, project_.catalog, f);
                } catch (const std::exception& e) {
                    results_->waveError = str(e.what());
                }
                results_->curves[0] = longitudinalAberration(project_.system, project_.catalog);
                results_->curves[1] = fieldCurvature(project_.system, project_.catalog);
                try {
                    results_->curves[2] = chromaticFocus(project_.system, project_.catalog);
                } catch (...) {
                }
                results_->curves[3] = geometricMTF(results_->spots[field_]);
                QString focus;
                try {
                    auto p = paraxial(project_.system, project_.catalog,
                                      project_.system.wavelengths[project_.system.primary].um);
                    focus =
                        QString("EFL %1 мм · f/%2").arg(p.efl, 0, 'f', 3).arg(p.fNumber, 0, 'f', 2);
                } catch (...) {
                    focus = "Параксиальный расчёт неприменим";
                }
                if (results_->spots[field_].samples.empty())
                    focus += " · Все лучи отсечены";
                status_->setText(QString("Режим: последовательный    %1    Полей: %2    Волн: %3   "
                                         " RMS: %4 мкм    Готово")
                                     .arg(focus)
                                     .arg(project_.system.fields.size())
                                     .arg(project_.system.wavelengths.size())
                                     .arg(results_->spots[field_].rms * 1000, 0, 'f', 3));
            } catch (const std::exception& e) {
                results_->error = str(e.what());
            }
        if (!results_->error.isEmpty())
            status_->setText(results_->error);
    } else if (!sceneBusy_)
        status_->setText(
            QString(
                "Непоследовательный · Тел %1 · Источников %2 · Лучей/ист. %3 · Сегментов ≤ %4%5")
                .arg(project_.scene.objects.size())
                .arg(project_.scene.sources.size())
                .arg(project_.scene.rayCount)
                .arg(project_.scene.maxSegments)
                .arg(results_->scene ? " · Расчёт сохранён в памяти" : " · Готов к расчёту"));
    if (project_.mode == 1) {
        auto errors = project_.scene.validate(project_.catalog);
        if (!errors.empty()) {
            results_->error = str(errors.front());
            status_->setText(results_->error);
        }
    }
    updatePlots();
}
void Window::editSurface(int r, int c) {
    const int tableRow = r;
    --r;
    if (building_ || r < 0 || r >= int(project_.system.surfaces.size()) || c == 0 || c == 2 ||
        c == 5)
        return;
    auto* item = surfaces_->item(tableRow, c);
    if (!item)
        return;
    if ((c == 3 || c == 4) && isSolved(project_.system,
            c == 3 ? SolveParameter::Radius : SolveParameter::Thickness, size_t(r))) {
        rebuildEditors();
        return;
    }
    auto candidate = project_.system.surfaces[r];
    if (c == 1)
        candidate.name = item->text().toStdString();
    else {
        bool ok;
        double v = item->text().toDouble(&ok);
        if (!ok || !std::isfinite(v)) {
            rebuildEditors();
            return;
        }
        double* dst = nullptr;
        switch (c) {
        case 3:
            dst = &candidate.radius;
            break;
        case 4:
            dst = &candidate.thickness;
            break;
        case 6:
            dst = &candidate.semiDiameter;
            break;
        case 7:
            dst = &candidate.conic;
            break;
        case 8:
            dst = &candidate.decenter.x;
            break;
        case 9:
            dst = &candidate.decenter.y;
            break;
        case 10:
            dst = &candidate.decenter.z;
            break;
        case 11:
            dst = &candidate.tilt.x;
            break;
        case 12:
            dst = &candidate.tilt.y;
            break;
        case 13:
            dst = &candidate.tilt.z;
            break;
        case 14:
            dst = &candidate.transmission;
            break;
        case 15:
            dst = &candidate.reflectivity;
            break;
        default:
            if (c >= 16 && c < 20)
                dst = &candidate.asphere[c - 16];
        }
        if (!dst)
            return;
        *dst = v;
    }
    auto system = project_.system;
    system.surfaces[r] = candidate;
    try { applySolves(system, project_.catalog); }
    catch (const std::exception& e) { rebuildEditors(); error(e); return; }
    checkpoint();
    project_.system = std::move(system);
    changed(false);
    rebuildTree();
}
void Window::solveParameter(int column) {
    if (selected_ < 0 || selected_ >= int(project_.system.surfaces.size())) return;
    const auto revision = revision_;
    SolveEditor dialog(project_, size_t(selected_), column == 3 ? SolveParameter::Radius : SolveParameter::Thickness, this);
    if (dialog.exec() == QDialog::Accepted) {
        if (revision != revision_) {
            error(std::runtime_error("Система изменилась, пока открыт редактор связи. Откройте его заново."));
            return;
        }
        checkpoint();
        project_.system = dialog.system();
        changed();
    }
}
void Window::editObject(int r, int c) {
    if (building_ || r < 0 || r >= int(project_.scene.objects.size()) || c == 0 || c == 2 ||
        c == 3 || c == 15)
        return;
    auto* item = objects_->item(r, c);
    if (!item)
        return;
    auto o = project_.scene.objects[r];
    if (c == 1)
        o.name = item->text().toStdString();
    else {
        bool ok;
        double v = item->text().toDouble(&ok);
        if (!ok || !std::isfinite(v)) {
            rebuildEditors();
            return;
        }
        double* dst = nullptr;
        switch (c) {
        case 4:
            dst = &o.pose.position.x;
            break;
        case 5:
            dst = &o.pose.position.y;
            break;
        case 6:
            dst = &o.pose.position.z;
            break;
        case 7:
            dst = &o.pose.tilt.x;
            break;
        case 8:
            dst = &o.pose.tilt.y;
            break;
        case 9:
            dst = &o.pose.tilt.z;
            break;
        case 10:
            dst = &o.size.x;
            break;
        case 11:
            dst = &o.size.y;
            break;
        case 12:
            dst = &o.size.z;
            break;
        case 13:
            dst = &o.radius1;
            break;
        case 14:
            dst = &o.radius2;
            break;
        case 16:
            dst = &o.reflectivity;
            break;
        case 17:
        case 18:
            if (v < 1 || v > 512 || std::floor(v) != v) {
                rebuildEditors();
                return;
            }
            if (c == 17)
                o.nx = int(v);
            else
                o.ny = int(v);
            break;
        }
        if (dst)
            *dst = v;
    }
    checkpoint();
    project_.scene.objects[r] = o;
    changed(false);
    rebuildTree();
}
void Window::editSource(int r, int c) {
    if (building_ || r < 0 || r >= int(project_.scene.sources.size()) || c == 0 || c == 2 || c == 3)
        return;
    auto* item = sources_->item(r, c);
    if (!item)
        return;
    auto src = project_.scene.sources[r];
    if (c == 1)
        src.name = item->text().toStdString();
    else {
        bool ok;
        double v = item->text().toDouble(&ok);
        if (!ok || !std::isfinite(v)) {
            rebuildEditors();
            return;
        }
        double* dst = nullptr;
        switch (c) {
        case 4:
            dst = &src.pose.position.x;
            break;
        case 5:
            dst = &src.pose.position.y;
            break;
        case 6:
            dst = &src.pose.position.z;
            break;
        case 7:
            dst = &src.pose.tilt.x;
            break;
        case 8:
            dst = &src.pose.tilt.y;
            break;
        case 9:
            dst = &src.pose.tilt.z;
            break;
        case 10:
            dst = &src.width;
            break;
        case 11:
            dst = &src.height;
            break;
        case 12:
            dst = &src.coneAngle;
            break;
        case 13:
            dst = &src.power;
            break;
        case 14:
            dst = &src.wavelength;
            break;
        }
        if (dst)
            *dst = v;
    }
    checkpoint();
    project_.scene.sources[r] = src;
    changed(false);
    rebuildTree();
}
bool Window::addSurface() {
    auto candidate = project_;
    Surface s;
    auto& surfaces = candidate.system.surfaces;
    const size_t count = surfaces.size();
    size_t at = std::clamp(selected_ + 1, 0, int(surfaces.size()));
    surfaces.insert(surfaces.begin() + at, s);
    reindexOptimization(candidate, at, 0, 1);
    std::vector<size_t> map(count + 1);
    for (size_t i = 0; i <= count; ++i) map[i] = i < at ? i : i + 1;
    if (at <= candidate.system.stop) ++candidate.system.stop;
    try { reindexSolves(candidate.system, map, candidate.catalog); }
    catch (const std::exception& e) { error(e); return false; }
    checkpoint();
    project_ = std::move(candidate);
    selected_ = int(at);
    changed();
    return true;
}
void Window::addLens() {
    auto candidate = project_;
    auto& surfaces = candidate.system.surfaces;
    const size_t count = surfaces.size();
    Surface front, back;
    front.name = "Линза · передняя";
    front.radius = 50;
    front.thickness = 5;
    front.material = "N-BK7";
    back.name = "Линза · задняя";
    back.radius = -50;
    back.thickness = 20;
    size_t at = std::clamp(selected_ + 1, 0, int(surfaces.size()));
    surfaces.insert(surfaces.begin() + at, {front, back});
    reindexOptimization(candidate, at, 0, 2);
    std::vector<size_t> map(count + 1);
    for (size_t i = 0; i <= count; ++i) map[i] = i < at ? i : i + 2;
    if (at <= candidate.system.stop) candidate.system.stop += 2;
    try { reindexSolves(candidate.system, map, candidate.catalog); }
    catch (const std::exception& e) { error(e); return; }
    checkpoint();
    project_ = std::move(candidate);
    selected_ = int(at);
    changed();
}
void Window::moveSurface(int step) {
    auto candidate = project_;
    auto& s = candidate.system;
    int to = selected_ + step;
    if (selected_ < 0 || to < 0 || to >= int(s.surfaces.size()))
        return;
    std::swap(s.surfaces[selected_], s.surfaces[to]);
    if (candidate.optimization)
        for (auto& v : candidate.optimization->variables)
            if (v.parameter != VariableParameter::Defocus) {
                if (v.surface == size_t(selected_)) v.surface = size_t(to);
                else if (v.surface == size_t(to)) v.surface = size_t(selected_);
            }
    reindexOptimization(candidate, 0, 0, 0);
    std::vector<size_t> map(s.surfaces.size() + 1);
    for (size_t i = 0; i < map.size(); ++i) map[i] = i;
    std::swap(map[selected_], map[to]);
    if (s.stop == size_t(selected_))
        s.stop = to;
    else if (s.stop == size_t(to))
        s.stop = selected_;
    try { reindexSolves(s, map, candidate.catalog); }
    catch (const std::exception& e) { error(e); return; }
    checkpoint();
    project_ = std::move(candidate);
    selected_ = to;
    changed();
}
void Window::removeRow() {
    if (project_.mode == 0) {
        auto candidate = project_;
        auto& s = candidate.system;
        if (s.surfaces.size() <= 1 || selected_ < 0 || selected_ >= int(s.surfaces.size()))
            return;
        const size_t count = s.surfaces.size();
        s.surfaces.erase(s.surfaces.begin() + selected_);
        reindexOptimization(candidate, size_t(selected_), 1, 0);
        std::vector<size_t> map(count + 1);
        for (size_t i = 0; i <= count; ++i)
            map[i] = i < size_t(selected_) ? i : i == size_t(selected_) ? SIZE_MAX : i - 1;
        if (s.stop > size_t(selected_))
            --s.stop;
        s.stop = std::min(s.stop, s.surfaces.size() - 1);
        try { reindexSolves(s, map, candidate.catalog); }
        catch (const std::exception& e) { error(e); return; }
        checkpoint();
        project_ = std::move(candidate);
    } else {
        auto& objects = project_.scene.objects;
        if (objects.size() <= 1 || selected_ < 0 || selected_ >= int(objects.size()))
            return;
        checkpoint();
        objects.erase(objects.begin() + selected_);
    }
    selected_ = 0;
    changed();
}
void Window::addObject(ObjectKind kind) {
    checkpoint();
    SceneObject o;
    o.kind = kind;
    if (kind == ObjectKind::Lens)
        o.size.z = 5;
    o.name = objectKinds[int(kind)].toStdString() + " " +
             std::to_string(project_.scene.objects.size() + 1);
    o.pose.position.z = kind == ObjectKind::Detector ? 100 : 40;
    if (kind == ObjectKind::Mirror) {
        o.interaction = Interaction::Reflect;
        o.size.z = .1;
    }
    if (kind == ObjectKind::Box || kind == ObjectKind::Cylinder) {
        o.interaction = Interaction::Absorb;
    }
    if (kind == ObjectKind::Sphere)
        o.size.z = o.size.y = o.size.x;
    if (kind == ObjectKind::Prism)
        o.size = {20, 16, 10 * sqrt(3.)};
    if (kind == ObjectKind::Detector) {
        o.size = {40, 40, .1};
        o.material = "AIR";
    }
    project_.scene.objects.push_back(o);
    selected_ = int(project_.scene.objects.size() - 1);
    changed();
}
void Window::addSource(SourceShape shape) {
    checkpoint();
    Source s;
    s.shape = shape;
    s.name =
        shapes[int(shape)].toStdString() + " " + std::to_string(project_.scene.sources.size() + 1);
    project_.scene.sources.push_back(s);
    changed();
    editors_->setCurrentIndex(1);
}
void Window::autofocus() {
    try {
        auto candidate = project_.system;
        optics::autofocus(candidate, project_.catalog);
        checkpoint();
        project_.system = candidate;
        changed();
        recalculate();
    } catch (const std::exception& e) {
        error(e);
    }
}
void Window::parameters() {
    QDialog dialog(this);
    dialog.setWindowTitle("Параметры последовательной системы");
    auto* form = new QFormLayout(&dialog);
    const auto s = project_.system;
    const auto revision = revision_;
    auto* name = new QLineEdit(str(s.name));
    form->addRow("Название", name);
    auto* type = new QComboBox;
    type->addItems({"Диаметр входного зрачка, мм", "Диафрагменное число f/#",
                    "NA в пространстве изображения (параксиально)"});
    form->addRow("Апертура", type);
    auto* aperture = spin(s.pupilDiameter, .0001, 100000);
    form->addRow("Значение", aperture);
    auto* distance = spin(s.objectDistance, 0, 1e8);
    form->addRow("Расстояние объекта, мм (0 = ∞)", distance);
    auto* defocus = spin(s.defocus, -10000, 10000);
    form->addRow("Дефокус, мм", defocus);
    auto* grid = integerSpin(s.pupilGrid, 3, 65);
    form->addRow("Сетка зрачка (нечётное число)", grid);
    auto* fields = new QTextEdit;
    fields->setFixedHeight(95);
    QString fs;
    for (auto f : s.fields)
        fs += QString("%1 %2 %3\n").arg(f.x).arg(f.y).arg(f.weight);
    fields->setPlainText(fs);
    form->addRow("Поля: X° Y° вес\nпо одному в строке", fields);
    auto* waves = new QTextEdit;
    waves->setFixedHeight(95);
    QString ws;
    for (auto w : s.wavelengths)
        ws += QString("%1 %2\n").arg(w.um, 0, 'g', 10).arg(w.weight);
    waves->setPlainText(ws);
    form->addRow("Волны: λ, мкм и вес", waves);
    auto* primary = integerSpin(s.primary + 1, 1, 20);
    form->addRow("Первичная волна (номер с 1)", primary);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (revision != revision_) throw std::invalid_argument("Проект изменился: откройте параметры заново");
            auto candidate = s;
            candidate.name = name->text().toStdString();
            candidate.objectDistance = distance->value();
            candidate.defocus = defocus->value();
            candidate.pupilGrid = grid->value() | 1;
            candidate.fields.clear();
            candidate.wavelengths.clear();
            std::istringstream fin(fields->toPlainText().toStdString());
            double x, y, weight;
            std::string line;
            while (std::getline(fin, line)) {
                if (line.find_first_not_of(" \t\r") == std::string::npos)
                    continue;
                std::istringstream row(line);
                if (!(row >> x >> y >> weight))
                    throw std::invalid_argument("Поля: в каждой строке нужны X Y вес");
                candidate.fields.push_back({x, y, weight});
            }
            std::istringstream win(waves->toPlainText().toStdString());
            double w;
            while (std::getline(win, line)) {
                if (line.find_first_not_of(" \t\r") == std::string::npos)
                    continue;
                std::istringstream row(line);
                if (!(row >> w >> weight))
                    throw std::invalid_argument("Волны: в каждой строке нужны длина волны и вес");
                candidate.wavelengths.push_back({w, weight});
            }
            candidate.primary = primary->value() - 1;
            candidate.pupilDiameter = aperture->value();
            if (type->currentIndex() != 0) {
                if (candidate.primary >= candidate.wavelengths.size())
                    throw std::invalid_argument("Первичная волна не существует");
                double f = std::abs(paraxial(candidate, project_.catalog,
                                             candidate.wavelengths[candidate.primary].um)
                                        .efl);
                if (type->currentIndex() == 1)
                    candidate.pupilDiameter = f / aperture->value();
                else {
                    double na = aperture->value();
                    if (na >= 1)
                        throw std::invalid_argument("NA должна быть меньше 1");
                    candidate.pupilDiameter = 2 * f * tan(asin(na));
                }
            }
            applySolves(candidate, project_.catalog);
            auto errors = candidate.validate(project_.catalog);
            if (!errors.empty())
                throw std::invalid_argument(errors.front());
            checkpoint();
            project_.system = candidate;
            if (project_.optimization) {
                std::erase_if(project_.optimization->operands, [&](auto o) {
                    return o.field >= 0 && size_t(o.field) >= candidate.fields.size();
                });
                if (project_.optimization->operands.empty()) project_.optimization.reset();
            }
            field_ = 0;
            changed();
            dialog.accept();
        } catch (const std::exception& e) {
            error(e);
        }
    });
    dialog.exec();
}
void Window::calculationParameters() {
    QDialog dialog(this);
    dialog.setWindowTitle("Параметры непоследовательного расчёта");
    auto* form = new QFormLayout(&dialog);
    auto& s = project_.scene;
    auto* name = new QLineEdit(str(s.name));
    auto* rays = integerSpin(s.rayCount, 1, 5000000);
    auto* display = integerSpin(s.displayRays, 0, 1000);
    auto* segments = integerSpin(s.maxSegments, 1, 500);
    auto* seed = new QLineEdit(QString::number(s.seed));
    form->addRow("Название", name);
    form->addRow("Лучей на источник", rays);
    form->addRow("Отображать траекторий", display);
    form->addRow("Предел сегментов", segments);
    form->addRow("Зерно генератора (повторяемость)", seed);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        bool ok;
        auto value = seed->text().toULongLong(&ok);
        if (!ok)
            return;
        checkpoint();
        s.name = name->text().toStdString();
        s.rayCount = rays->value();
        s.displayRays = display->value();
        s.maxSegments = segments->value();
        s.seed = value;
        changed();
        dialog.accept();
    });
    dialog.exec();
}
void Window::catalog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Каталог оптических материалов");
    dialog.resize(690, 350);
    auto* layout = new QVBoxLayout(&dialog);
    auto* table = new QTableWidget;
    configureTable(table, {"Материал", "nd", "Vd", "nF", "nC"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setRowCount(project_.catalog.materials.size());
    int r = 0;
    for (auto m : project_.catalog.materials) {
        QStringList values = {str(m.name), num(m.index(.5875618)), num(m.vd),
                              num(m.index(.4861327)), num(m.index(.6562725))};
        for (int c = 0; c < 5; ++c)
            table->setItem(r, c, new QTableWidgetItem(values[c]));
        ++r;
    }
    auto* search = new QLineEdit;
    search->setPlaceholderText("Поиск по названию материала");
    layout->addWidget(search);
    layout->addWidget(table);
    connect(search, &QLineEdit::textChanged, &dialog, [table](const QString& text) {
        for (int i = 0; i < table->rowCount(); ++i)
            table->setRowHidden(i, !table->item(i, 0)->text().contains(text, Qt::CaseInsensitive));
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, &dialog,
            &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
        int r = table->currentRow();
        if (r < 0 || selected_ < 0)
            return;
        auto m = project_.catalog.materials[r].name;
        checkpoint();
        if (project_.mode == 0 && selected_ < int(project_.system.surfaces.size()))
            project_.system.surfaces[selected_].material = m;
        else if (project_.mode == 1 && selected_ < int(project_.scene.objects.size()))
            project_.scene.objects[selected_].material = m;
        changed();
        dialog.accept();
    });
    dialog.exec();
}
void Window::manualMaterial() {
    QDialog dialog(this);
    dialog.setWindowTitle("Материал по nd и числу Аббе");
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit("CUSTOM");
    auto* nd = spin(1.5, 1, 4, 7);
    auto* vd = spin(60, 0, 200);
    form->addRow("Наименование", name);
    form->addRow("nd", nd);
    form->addRow("Vd (0 = постоянный n)", vd);
    form->addRow(new QLabel("Дисперсия приближённо восстанавливается по модели Коши."));
    auto* b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(b);
    connect(b, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(b, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            Material m;
            m.name = name->text().toStdString();
            m.nd = nd->value();
            m.vd = vd->value();
            auto staged = project_.catalog;
            staged.add(m);
            checkpoint();
            project_.catalog = staged;
            changed();
            dialog.accept();
        } catch (const std::exception& e) {
            error(e);
        }
    });
    dialog.exec();
}
void Window::importCatalog() {
    auto path = QFileDialog::getOpenFileName(this, "Импорт Zemax AGF · Sellmeier 1", {},
                                             "AGF (*.agf *.AGF)");
    if (path.isEmpty())
        return;
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 10 * 1024 * 1024)
            throw std::runtime_error("Невозможно открыть каталог");
        auto staged = project_.catalog;
        staged.importAGF(file.readAll().toStdString());
        checkpoint();
        project_.catalog = staged;
        changed();
    } catch (const std::exception& e) {
        error(e);
    }
}
void Window::runScene() {
    if (sceneBusy_ || optimizationBusy_)
        return;
    auto errors = project_.scene.validate(project_.catalog);
    if (!errors.empty()) {
        QMessageBox::warning(this, "Проверка сцены", str(errors.front()));
        return;
    }
    recalculate();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = cancel_;
    auto scene = project_.scene;
    auto catalog = project_.catalog;
    jobRevision_ = revision_;
    sceneBusy_ = true;
    progress_->setRange(0, 0);
    progress_->show();
    status_->setText("Трассировка сцены…");
    if (auto* trace = findChild<QToolButton*>("traceSceneButton"))
        trace->setText("Остано-\nвить");
    watcher_.setFuture(QtConcurrent::run([scene, catalog, cancel] {
        return traceScene(scene, catalog, [cancel](size_t, size_t) { return !cancel->load(); });
    }));
}
void Window::optimizationSettings() {
    try {
        OptimizationEditor dialog(project_, this);
        if (dialog.exec() != QDialog::Accepted) return;
        checkpoint();
        project_.optimization = dialog.plan();
        changed(false);
    } catch (const std::exception& e) { error(e); }
}
void Window::optimize(bool configured) {
    if (project_.mode || optimizationBusy_ || sceneBusy_) return;
    if (configured && !project_.optimization) {
        optimizationSettings();
        if (!project_.optimization) return;
    }
    auto project = project_;
    const auto startRevision = revision_;
    optimizationCancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = optimizationCancel_;
    optimizationBusy_ = true;
    progress_->setRange(0, 0);
    progress_->show();
    status_->setText(configured ? "Оптимизация по заданным переменным и критериям…"
                                : "Оптимизация радиусов и положения изображения…");
    disconnect(&optimizationWatcher_, nullptr, this, nullptr);
    connect(&optimizationWatcher_, &QFutureWatcherBase::finished, this,
            [this, project, startRevision, configured, cancel] {
        progress_->hide();
        optimizationBusy_ = false;
        try {
            auto result = optimizationWatcher_.result();
            if (cancel->load() || result.second.cancelled) {
                status_->setText("Оптимизация остановлена. Система сохранена без изменений.");
                return;
            }
            if (revision_ != startRevision) {
                status_->setText("Система изменилась — результат оптимизации отброшен");
                return;
            }
            checkpoint();
            project_.system = result.first;
            changed();
            recalculate();
            QString report;
            if (configured) {
                auto initial = project.system;
                const auto& plan = *project.optimization;
                if (plan.refocus) optics::autofocus(initial, project.catalog);
                auto before = evaluateMerit(initial, project.catalog, plan);
                auto after = evaluateMerit(result.first, project.catalog, plan);
                report = QString("Безразмерная функция качества: %1 → %2\nПроверок вариантов: %3\n"
                                 "Сетка зрачка: %4 × %4; проходов: %5\n\nПеременные (до → после):\n")
                             .arg(result.second.before, 0, 'g', 10)
                             .arg(result.second.after, 0, 'g', 10)
                             .arg(result.second.evaluations).arg(plan.pupilGrid).arg(plan.iterations);
                for (auto v : plan.variables)
                    report += QString("%1 · %2: %3 → %4 [%5…%6]\n")
                                  .arg(v.parameter == VariableParameter::Defocus ? "Система" : QString("S%1").arg(v.surface + 1))
                                  .arg(parameterName(v.parameter)).arg(variableValue(initial, v), 0, 'g', 12)
                                  .arg(variableValue(result.first, v), 0, 'g', 12)
                                  .arg(v.lower, 0, 'g', 12).arg(v.upper, 0, 'g', 12);
                report += "\nКритерии: значение до → после; цель; масштаб; вес\n";
                for (size_t i = 0; i < plan.operands.size(); ++i) {
                    auto o = plan.operands[i];
                    report += QString("%1 · %2: %3 → %4; %5; %6; %7\n")
                                  .arg(meritName(o.kind), o.field < 0 ? "все поля" : QString("поле %1").arg(o.field + 1))
                                  .arg(before.values[i], 0, 'g', 12).arg(after.values[i], 0, 'g', 12)
                                  .arg(o.target, 0, 'g', 12).arg(o.scale, 0, 'g', 12).arg(o.weight, 0, 'g', 12);
                }
                report += "\nКритерий после каждого прохода:\n";
                for (size_t i = 0; i < result.second.history.size(); ++i)
                    report += QString("%1: %2\n").arg(i).arg(result.second.history[i], 0, 'g', 12);
            } else {
                report = QString("RMS + штраф за изменение EFL: %1 → %2 мкм\n"
                                 "Проверок вариантов: %3\nПеременные: неплоские радиусы; автофокус после каждой попытки.")
                             .arg(result.second.before * 1000, 0, 'f', 4)
                             .arg(result.second.after * 1000, 0, 'f', 4).arg(result.second.evaluations);
            }
            auto* dialog = new QDialog(this);
            dialog->setObjectName("optimizationReportDialog");
            dialog->setWindowTitle("Результат оптимизации");
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->resize(840, 560);
            auto* layout = new QVBoxLayout(dialog);
            auto* text = new QPlainTextEdit;
            text->setObjectName("optimizationReportText");
            text->setReadOnly(true);
            text->setPlainText(report);
            layout->addWidget(text);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
            buttons->button(QDialogButtonBox::Close)->setText("Закрыть");
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
            dialog->show();
        } catch (const std::exception& e) { error(e); }
    });
    optimizationWatcher_.setFuture(QtConcurrent::run([project, configured, cancel]() mutable {
        auto result = configured
            ? optics::optimize(project.system, project.catalog, *project.optimization,
                               [cancel](size_t, size_t, double) { return !cancel->load(); })
            : optimizeRadii(project.system, project.catalog);
        return std::make_pair(project.system, result);
    }));
}
void Window::exportDetectorStatistics() {
    if (!results_ || !results_->scene || results_->detectorStats.empty()) {
        status_->setText("Сначала рассчитайте карту детектора");
        return;
    }
    const size_t index = std::min(size_t(results_->detector), results_->detectorStats.size() - 1);
    auto path = QFileDialog::getSaveFileName(this, "Сводка выбранного детектора", {}, "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    auto csv = detectorStatisticsCSV(results_->scene->detectors.at(index), results_->detectorStats[index],
                                     results_->scene->launchedPower, results_->scene->cancelled);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(csv) != csv.size() || !file.commit()) {
        std::runtime_error e("Не удалось сохранить сводку детектора");
        error(e);
    }
}
void Window::exportCSV() {
    if (!results_)
        return;
    auto* plot = dynamic_cast<PlotWidget*>(views_->currentWidget());
    if (!plot)
        return;
    auto path =
        QFileDialog::getSaveFileName(this, "Выгрузить " + viewName(plot->view), {}, "CSV (*.csv)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive))
        path += ".csv";
    QByteArray csv;
    auto line = [&](QString value) { csv += value.toUtf8() + "\n"; };
    auto& d = *results_;
    if (plot->view == View::Detector) {
        if (!d.scene || d.scene->detectors.empty())
            return;
        auto& dt = d.scene->detectors.at(d.detector);
        auto& object = project_.scene.objects[dt.objectIndex];
        line("x_mm,y_mm,power_W,irradiance_W_per_mm2");
        for (int y = 0; y < dt.ny; ++y)
            for (int x = 0; x < dt.nx; ++x) {
                double watts = dt.watts[y * dt.nx + x];
                line(QString("%1,%2,%3,%4")
                         .arg((x + .5) / dt.nx * object.size.x - object.size.x / 2, 0, 'g', 14)
                         .arg((y + .5) / dt.ny * object.size.y - object.size.y / 2, 0, 'g', 14)
                         .arg(watts, 0, 'g', 14)
                         .arg(watts / dt.cellArea, 0, 'g', 14));
            }
    } else if (plot->view == View::DetectorProfile) {
        if (!d.scene || d.detectorStats.empty()) return;
        csv = detectorProfileCSV(d.detectorStats.at(d.detector));
    } else if (plot->view == View::Scene) {
        if (!d.scene)
            return;
        line("ray,segment,x_mm,y_mm,z_mm,source_index,wavelength_um,incoming_segment_kind");
        for (size_t r = 0; r < d.scene->paths.size(); ++r)
            for (size_t i = 0; i < d.scene->paths[r].size(); ++i) {
                auto p = d.scene->paths[r][i];
                const auto source = r < d.scene->pathSources.size()
                                        ? QString::number(d.scene->pathSources[r] + 1)
                                        : QString();
                const auto wavelength = r < d.scene->pathWavelengths.size()
                                            ? QString::number(d.scene->pathWavelengths[r], 'g', 14)
                                            : QString();
                const auto kind =
                    i && r < d.scene->pathKinds.size() && i - 1 < d.scene->pathKinds[r].size()
                        ? QString::number(d.scene->pathKinds[r][i - 1])
                        : QString();
                line(QString("%1,%2,%3,%4,%5,%6,%7,%8")
                         .arg(r)
                         .arg(i)
                         .arg(p.x, 0, 'g', 14)
                         .arg(p.y, 0, 'g', 14)
                         .arg(p.z, 0, 'g', 14)
                         .arg(source, wavelength, kind));
            }
    } else if (plot->view == View::Fan) {
        if (!d.fanError.isEmpty())
            return;
        csv = rayFanCSV(d.fan);
    } else if (plot->view == View::OPD || plot->view == View::Wavefront) {
        line("pupil_x,pupil_y,OPD_mm,OPD_waves");
        for (auto p : d.wave.samples)
            line(QString("%1,%2,%3,%4")
                     .arg(p.px, 0, 'g', 14)
                     .arg(p.py, 0, 'g', 14)
                     .arg(p.opd, 0, 'g', 14)
                     .arg(p.opd * 1000 / d.wave.wavelength, 0, 'g', 14));
    } else if (plot->view == View::MTF) {
        line("frequency_lp_per_mm,MTF_X,MTF_Y");
        for (size_t i = 0; i < d.diffraction.frequency.size(); ++i)
            line(QString("%1,%2,%3")
                     .arg(d.diffraction.frequency[i], 0, 'g', 14)
                     .arg(d.diffraction.mtfX[i], 0, 'g', 14)
                     .arg(d.diffraction.mtfY[i], 0, 'g', 14));
    } else if (plot->view == View::PSF) {
        line("x_um,y_um,normalized_energy");
        int n = d.diffraction.size;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                line(QString("%1,%2,%3")
                         .arg((x - n / 2) * d.diffraction.pixelUm, 0, 'g', 14)
                         .arg((y - n / 2) * d.diffraction.pixelUm, 0, 'g', 14)
                         .arg(d.diffraction.psf[y * n + x], 0, 'g', 14));
    } else if (int(plot->view) >= 10 && int(plot->view) <= 13) {
        auto& curve = d.curves[int(plot->view) - 10];
        QString header = "x";
        for (size_t j = 0; j < curve.y.size(); ++j)
            header += ",series_" + QString::number(j + 1);
        line(header);
        for (size_t i = 0; i < curve.x.size(); ++i) {
            QString row = QString::number(curve.x[i], 'g', 14);
            for (auto series : curve.y)
                row += "," + QString::number(series[i], 'g', 14);
            line(row);
        }
    } else if (plot->view == View::RMS) {
        line("field,x_deg,y_deg,RMS_um,valid_rays,launched_rays");
        for (size_t i = 0; i < d.spots.size(); ++i)
            line(QString("%1,%2,%3,%4,%5,%6")
                     .arg(i + 1)
                     .arg(project_.system.fields[i].x)
                     .arg(project_.system.fields[i].y)
                     .arg(d.spots[i].rms * 1000, 0, 'g', 14)
                     .arg(d.spots[i].samples.size())
                     .arg(d.spots[i].launched));
    } else if (plot->view == View::Layout) {
        line("field,ray,segment,x_mm,y_mm,z_mm");
        for (size_t f = 0; f < project_.system.fields.size(); ++f)
            for (int r = -5; r <= 5; ++r) {
                auto t = trace(
                    project_.system, project_.catalog,
                    pupilRay(project_.system, project_.catalog, project_.system.fields[f],
                             project_.system.wavelengths[project_.system.primary].um, 0, r / 5.));
                for (size_t i = 0; i < t.points.size(); ++i) {
                    auto p = t.points[i];
                    line(QString("%1,%2,%3,%4,%5,%6")
                             .arg(f + 1)
                             .arg(r + 5)
                             .arg(i)
                             .arg(p.x, 0, 'g', 14)
                             .arg(p.y, 0, 'g', 14)
                             .arg(p.z, 0, 'g', 14));
                }
            }
    } else {
        if (d.spots.empty())
            return;
        line("pupil_x,pupil_y,wavelength_um,image_x_mm,image_y_mm,power_weight");
        for (auto p : d.spots.at(field_).samples)
            line(QString("%1,%2,%3,%4,%5,%6")
                     .arg(p.px)
                     .arg(p.py)
                     .arg(p.wavelength, 0, 'g', 14)
                     .arg(p.image.x, 0, 'g', 14)
                     .arg(p.image.y, 0, 'g', 14)
                     .arg(p.power, 0, 'g', 14));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(csv) != csv.size() || !file.commit())
        QMessageBox::warning(this, "Выгрузка", "Не удалось записать CSV");
}
bool Window::mayDiscard() {
    if (!dirty_)
        return true;
    auto answer =
        QMessageBox::question(this, "Несохранённый проект", "Сохранить изменения проекта?",
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        save();
        return !dirty_;
    }
    return true;
}
void Window::save(bool as) {
    QString path = path_;
    if (as || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Сохранить оптический проект", path_,
                                            "Optical CAD (*.optcad)");
    if (path.isEmpty())
        return;
    if (!path.endsWith(".optcad", Qt::CaseInsensitive))
        path += ".optcad";
    try {
        auto errors = project_.system.validate(project_.catalog),
             sceneErrors = project_.scene.validate(project_.catalog);
        if (!errors.empty())
            throw std::invalid_argument(errors.front());
        if (!sceneErrors.empty())
            throw std::invalid_argument(sceneErrors.front());
        project_.workspace["docks"] = QString::fromLatin1(saveState().toBase64());
        QJsonArray tabs;
        for (int i = 0; i < views_->count(); ++i)
            if (auto* p = dynamic_cast<PlotWidget*>(views_->widget(i)))
                tabs.append(int(p->view));
        project_.workspace[project_.mode ? "nonsequentialViews" : "sequentialViews"] = tabs;
        saveProject(path, project_);
        path_ = path;
        dirty_ = false;
        setWindowTitle("Optical CAD — Оптический модуль — " + QFileInfo(path_).fileName());
    } catch (const std::exception& e) {
        error(e);
    }
}
void Window::open() {
    if (!mayDiscard())
        return;
    auto path = QFileDialog::getOpenFileName(this, "Открыть проект", {}, "Optical CAD (*.optcad)");
    if (path.isEmpty())
        return;
    try {
        auto p = loadProject(path);
        setProject(std::move(p));
        path_ = path;
        setWindowTitle("Optical CAD — Оптический модуль — " + QFileInfo(path_).fileName());
    } catch (const std::exception& e) {
        error(e);
    }
}
void Window::closeEvent(QCloseEvent* e) {
    if (mayDiscard()) {
        cancel_->store(true);
        e->accept();
    } else
        e->ignore();
}
void Window::writeExamples(const QString& directory) {
    QDir().mkpath(directory);
    Project p;
    optics::autofocus(p.system, p.catalog);
    saveProject(directory + "/singlet.optcad", p);
    p.mode = 1;
    saveProject(directory + "/led_illuminator.optcad", p);
    p.mode = 0;
    p.system.name = "Ахроматический дублет";
    p.system.pupilDiameter = 8;
    p.system.fields = {{0, 0, 1}, {0, 2, 1}};
    Surface a, b, c;
    a.name = "Крон · передняя";
    a.radius = 45;
    a.thickness = 5;
    a.material = "N-BK7";
    b.name = "Склейка крон/флинт";
    b.radius = -35;
    b.thickness = 2;
    b.material = "N-F2";
    c.name = "Флинт · задняя";
    c.radius = -110;
    c.thickness = 60;
    p.system.surfaces = {a, b, c};
    optics::autofocus(p.system, p.catalog);
    saveProject(directory + "/achromat.optcad", p);
    p.mode = 1;
    p.scene = Scene::prismDemo(p.catalog);
    saveProject(directory + "/spectral_prism.optcad", p);
    Project linked;
    linked.system.name = "Линза — связанный радиус и толщина края";
    ParameterSolve pickup;
    pickup.surface = 1; pickup.reference = 0; pickup.scale = -1;
    ParameterSolve edge;
    edge.parameter = SolveParameter::Thickness; edge.kind = SolveKind::EdgeThickness;
    edge.surface = 0; edge.reference = 1; edge.height = 12.5; edge.value = 1;
    linked.system.solves = {pickup, edge};
    applySolves(linked.system, linked.catalog);
    optics::autofocus(linked.system, linked.catalog);
    saveProject(directory + "/linked_singlet.optcad", linked);
    Project marginal;
    marginal.system.name = "Линза — фокус по краевому лучу";
    ParameterSolve ray;
    ray.parameter = SolveParameter::Thickness; ray.kind = SolveKind::MarginalHeight;
    ray.surface = 1; ray.reference = 2; ray.value = 0; ray.pupil = 1;
    marginal.system.solves = {ray};
    applySolves(marginal.system, marginal.catalog);
    saveProject(directory + "/marginal_focus.optcad", marginal);
}
static void reportDialog(QWidget* parent, QString title, QString text) {
    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.resize(900, 580);
    auto* layout = new QVBoxLayout(&dialog);
    auto* edit = new QPlainTextEdit(text);
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    layout->addWidget(buttons);
    QObject::connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, &dialog, [&] {
        auto path =
            QFileDialog::getSaveFileName(&dialog, "Сохранить числовой отчёт", {}, "Текст (*.txt)");
        if (path.isEmpty())
            return;
        QSaveFile f(path);
        auto bytes = text.toUtf8();
        if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size())
            f.commit();
    });
    dialog.exec();
}
void Window::rayReport() {
    QDialog dialog(this);
    dialog.setWindowTitle("Трассировка одиночного луча");
    auto* form = new QFormLayout(&dialog);
    auto* px = spin(0, -1, 1);
    auto* py = spin(.5, -1, 1);
    auto* field = new QComboBox;
    for (auto f : project_.system.fields)
        field->addItem(QString("%1° / %2°").arg(f.x).arg(f.y));
    auto* wave = new QComboBox;
    for (auto w : project_.system.wavelengths)
        wave->addItem(num(w.um) + " мкм");
    wave->setCurrentIndex(project_.system.primary);
    form->addRow("X в зрачке", px);
    form->addRow("Y в зрачке", py);
    form->addRow("Поле", field);
    form->addRow("Длина волны", wave);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted)
        return;
    try {
        auto& s = project_.system;
        auto errs = s.validate(project_.catalog);
        if (!errs.empty())
            throw std::invalid_argument(errs.front());
        auto t =
            trace(s, project_.catalog,
                  pupilRay(s, project_.catalog, s.fields.at(field->currentIndex()),
                           s.wavelengths.at(wave->currentIndex()).um, px->value(), py->value()));
        QString text = "ТОЧКИ ТРАЕКТОРИИ В ММ\n\nУзел\tX\tY\tZ\n";
        for (size_t i = 0; i < t.points.size(); ++i) {
            auto v = t.points[i];
            text += QString("%1\t%2\t%3\t%4\n")
                        .arg(i)
                        .arg(v.x, 0, 'g', 12)
                        .arg(v.y, 0, 'g', 12)
                        .arg(v.z, 0, 'g', 12);
        }
        const QStringList statuses = {"Луч достиг изображения", "Нет пересечения",
                                      "Отсечён апертурой", "Полное внутреннее отражение",
                                      "Некорректные данные"};
        text += QString("\nСтатус: %1\nOPL до последней физической поверхности: %2 "
                        "мм\nОтносительная мощность: %3\nВыходное направление: %4, %5, %6\n")
                    .arg(statuses[int(t.status)])
                    .arg(t.opl, 0, 'g', 12)
                    .arg(t.power, 0, 'g', 12)
                    .arg(t.exitDirection.x, 0, 'g', 12)
                    .arg(t.exitDirection.y, 0, 'g', 12)
                    .arg(t.exitDirection.z, 0, 'g', 12);
        reportDialog(this, "Одиночный луч", text);
    } catch (const std::exception& e) {
        error(e);
    }
}
void Window::paraxialReport() {
    try {
        auto& s = project_.system;
        auto data = paraxial(s, project_.catalog, s.wavelengths[s.primary].um);
        QString text =
            QString("ПАРАКСИАЛЬНЫЙ РАСЧЁТ\nЦентрированные преломляющие поверхности · вектор (y, "
                    "n·u)\n\nEFL = %1 мм\nBFL = %2 мм\nf/# = %3\n\nABCD\n%4\t%5\n%6\t%7\n\n")
                .arg(data.efl, 0, 'g', 12)
                .arg(data.bfl, 0, 'g', 12)
                .arg(data.fNumber, 0, 'g', 12)
                .arg(data.matrix[0], 0, 'g', 12)
                .arg(data.matrix[1], 0, 'g', 12)
                .arg(data.matrix[2], 0, 'g', 12)
                .arg(data.matrix[3], 0, 'g', 12);
        text += "Поверхность\tВысота y, мм\tУгол u, рад\tИндекс n\n";
        double h = s.pupilDiameter / 2, u = 0, n = 1;
        for (size_t i = 0; i < s.surfaces.size(); ++i) {
            auto a = s.surfaces[i];
            double next = a.kind == SurfaceKind::Stop
                              ? n
                              : project_.catalog.get(a.material).index(s.wavelengths[s.primary].um);
            if (a.radius != 0)
                u -= (next - n) / a.radius * h;
            n = next;
            text += QString("%1\t%2\t%3\t%4\n")
                        .arg(i + 1)
                        .arg(h, 0, 'g', 12)
                        .arg(u / n, 0, 'g', 12)
                        .arg(n, 0, 'g', 12);
            h += a.thickness * u / n;
        }
        reportDialog(this, "Параксиальная трассировка", text);
    } catch (const std::exception& e) {
        error(e);
    }
}
void Window::prescription() {
    auto& s = project_.system;
    QString text =
        "ОПИСАНИЕ СИСТЕМЫ\n" + str(s.name) +
        "\nЕдиницы: мм, мкм, градусы\n\n№\tРадиус\tТолщина\tМатериал\tПолудиаметр\tКоника\n";
    for (size_t i = 0; i < s.surfaces.size(); ++i) {
        auto a = s.surfaces[i];
        text += QString("%1%2\t%3\t%4\t%5\t%6\t%7\n")
                    .arg(i + 1)
                    .arg(i == s.stop ? " STOP" : "")
                    .arg(a.radius, 0, 'g', 12)
                    .arg(a.thickness, 0, 'g', 12)
                    .arg(str(a.material))
                    .arg(a.semiDiameter, 0, 'g', 12)
                    .arg(a.conic, 0, 'g', 12);
    }
    text += "\nВолны, мкм (вес):\n";
    for (auto w : s.wavelengths)
        text += num(w.um) + " (" + num(w.weight) + ")\n";
    text += "\nПоля X°, Y° (вес):\n";
    for (auto f : s.fields)
        text += num(f.x) + ", " + num(f.y) + " (" + num(f.weight) + ")\n";
    reportDialog(this, "Описание системы", text);
}
