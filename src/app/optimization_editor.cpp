#include "number_input.hpp"
#include "optimization_editor.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

using namespace optics;
QString parameterName(VariableParameter p) {
    if (const int order = asphericOrder(p)) return QString("A%1, мм^(%2)").arg(order).arg(1-order);
    const QStringList names{"Радиус, мм", "Толщина, мм", "Коника", "A4, мм⁻³", "A6, мм⁻⁵",
                            "A8, мм⁻⁷", "A10, мм⁻⁹", "Дефокус, мм"};
    return names.value(int(p));
}
QString meritName(MeritKind k) {
    const QStringList names{"RMS пятна, мм", "EFL, мм", "BFL, мм", "Изображение Z, мм",
                            "Центр X, мм", "Центр Y, мм", "Пропускание, 0…1", "RMS OPD всех волн, мм", "MTF X, 0…1", "MTF Y, 0…1",
                            "Дисторсия, %", "Длина до последней поверхности, мм", "Толщина края, мм"};
    return names.value(int(k));
}
static void cell(QTableWidget* table, int row, int col, double value, bool editable = true) {
    auto* item = new QTableWidgetItem(QString::number(value, 'g', 12));
    if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, col, item);
}
static double readCell(const QTableWidget* table, int row, int col) {
    bool valid = false;
    const auto* item = table->item(row, col);
    double v = item ? uiNumber(item->text(), &valid) : 0;
    if (!valid || !std::isfinite(v))
        throw std::invalid_argument("В таблице нужно конечное число (десятичный разделитель — точка или запятая)");
    return v;
}
OptimizationEditor::OptimizationEditor(const Project& project, QWidget* parent)
    : QDialog(parent), project_(project) {
    setObjectName("optimizationDialog");
    setWindowTitle("Переменные и функция качества");
    resize(1180, 740);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        "Критерий = √(Σ вес × ((значение − цель) / масштаб)² / Σ вес). "
        "Для «Всех полей» квадраты отклонений усредняются с весами полей. "
        "RMS и центры учитывают все длины волн и их веса; OPD — отдельные невязки волн. "
        "MTF — полихроматическая, частота в циклах/мм, сетка зрачка 33. Для края задайте поверхность и высоту в мм.");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto* tabs = new QTabWidget;
    tabs->tabBar()->setExpanding(false);
    tabs->tabBar()->setUsesScrollButtons(false);
    tabs->tabBar()->setElideMode(Qt::ElideNone);
    layout->addWidget(tabs, 1);
    auto makePage = [&](QString title, QStringList headers, QTableWidget*& table, QString id) {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        table = new QTableWidget(0, headers.size());
        table->setObjectName(id);
        table->setHorizontalHeaderLabels(headers);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        v->addWidget(table);
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton("Добавить");
        add->setObjectName(id + "Add");
        auto* remove = new QPushButton("Удалить");
        remove->setObjectName(id + "Remove");
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch();
        v->addLayout(row);
        tabs->addTab(page, title);
        auto* target = table;
        connect(remove, &QPushButton::clicked, this, [target] {
            if (target->currentRow() >= 0) target->removeRow(target->currentRow());
        });
        return add;
    };
    auto* addVariable = makePage("Переменные",
        {"Поверхность", "Параметр", "Сейчас", "Минимум", "Максимум", "Начальный шаг"},
        variables_, "optimizationVariables");
    auto* addOperand = makePage("Функция качества",
        {"Критерий", "Поле", "Цель", "Масштаб", "Вес", "Значение", "Вклад", "Частота MTF", "Грань края", "Высота края"},
        operands_, "meritOperands");
    operands_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Interactive);
    operands_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Interactive);
    operands_->setColumnWidth(0,210);operands_->setColumnWidth(1,140);
    operands_->horizontalHeaderItem(7)->setToolTip("Пространственная частота полихроматической MTF, циклы/мм; сетка зрачка 33");
    operands_->horizontalHeaderItem(8)->setToolTip("Номер поверхности перед промежутком; следующая поверхность образует вторую границу края");
    operands_->horizontalHeaderItem(9)->setToolTip("Радиальная высота края, мм");
    connect(addVariable, &QPushButton::clicked, this, [this] {
        if (variables_->rowCount() < 64)
            appendVariable({VariableParameter::Thickness, 0, 0, 100, .25});
    });
    connect(addOperand, &QPushButton::clicked, this, [this] {
        if (operands_->rowCount() < 64) appendOperand({MeritKind::SpotRMS, -1, 0, .01, 1});
    });
    auto p = project.optimization.value_or(defaultOptimization(project.system, project.catalog));
    for (auto v : p.variables) appendVariable(v);
    for (auto o : p.operands) appendOperand(o);
    auto* methodRow=new QHBoxLayout;
    algorithm_=new QComboBox;algorithm_->setObjectName("optimizationAlgorithm");
    algorithm_->addItems({"Координатный поиск", "DLS / Levenberg–Marquardt"});
    algorithm_->setCurrentIndex(int(p.algorithm));
    starts_=new QSpinBox;starts_->setObjectName("optimizationStarts");starts_->setRange(1,8);starts_->setValue(int(p.starts));
    allFieldsFocus_=new QCheckBox("Фокус по всем полям и волнам");
    allFieldsFocus_->setObjectName("optimizationAllFieldsFocus");allFieldsFocus_->setChecked(p.allFieldsFocus);
    methodRow->addWidget(new QLabel("Алгоритм"));methodRow->addWidget(algorithm_);
    methodRow->addWidget(new QLabel("Стартов"));methodRow->addWidget(starts_);
    methodRow->addWidget(allFieldsFocus_);methodRow->addStretch();layout->addLayout(methodRow);
    auto* controls = new QHBoxLayout;
    iterations_ = new QSpinBox;
    iterations_->setObjectName("optimizationIterations");
    iterations_->setRange(1, 100);
    iterations_->setValue(int(p.iterations));
    grid_ = new QSpinBox;
    grid_->setObjectName("optimizationGrid");
    grid_->setRange(3, 33);
    grid_->setValue(p.pupilGrid);
    survival_ = new DecimalSpinBox;
    survival_->setObjectName("optimizationSurvival");
    survival_->setRange(0, 1);
    survival_->setDecimals(3);
    survival_->setSingleStep(.01);
    survival_->setValue(p.minimumThroughput);
    survival_->setToolTip("Минимальная доля дошедших лучей на каждом поле с весами волн, до потерь на покрытиях. 0 отключает ограничение.");
    refocus_ = new QCheckBox("Автофокус");
    refocus_->setObjectName("optimizationRefocus");
    refocus_->setChecked(p.refocus);
    refocus_->setToolTip("Последняя толщина и дефокус должны быть исключены из переменных. Без общего фокуса — первое поле/первичная волна; с ним — минимум взвешенной суммы RMS² всех полей и волн.");
    allFieldsFocus_->setEnabled(p.refocus);
    connect(refocus_,&QCheckBox::toggled,allFieldsFocus_,&QCheckBox::setEnabled);
    connect(algorithm_,&QComboBox::currentIndexChanged,this,[this]{invalidatePreview();});
    connect(starts_,&QSpinBox::valueChanged,this,[this]{invalidatePreview();});
    connect(allFieldsFocus_,&QCheckBox::toggled,this,[this]{invalidatePreview();});
    for (auto pair : {std::pair<QString, QWidget*>{"Проходов", iterations_},
                     {"Сетка зрачка", grid_}, {"Доля дошедших ≥", survival_}}) {
        controls->addWidget(new QLabel(pair.first));
        controls->addWidget(pair.second);
    }
    controls->addWidget(refocus_);
    controls->addStretch();
    layout->addLayout(controls);
    message_ = new QLabel("Изменения вступят в силу после сохранения настроек.");
    message_->setObjectName("meritPreviewMessage");
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* evaluate = buttons->addButton("Рассчитать критерий", QDialogButtonBox::ActionRole);
    evaluate->setObjectName("evaluateMeritButton");
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveOptimizationButton");
    buttons->button(QDialogButtonBox::Save)->setText("Сохранить настройки");
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    layout->addWidget(buttons);
    connect(evaluate, &QPushButton::clicked, this, [this] { preview(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        try {
            auto p = plan();
            auto errors = p.validate(project_.system);
            if (!errors.empty()) throw std::invalid_argument(errors.front());
            for (auto v : p.variables) {
                const double value = variableValue(project_.system, v);
                if (value < v.lower || value > v.upper)
                    throw std::invalid_argument("Начальное значение переменной выходит за границы");
            }
            accept();
        } catch (const std::exception& e) {
            message_->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(variables_, &QTableWidget::itemChanged, this, [this](auto* item) {
        if (item->column() >= 3) invalidatePreview();
    });
    connect(operands_, &QTableWidget::itemChanged, this, [this](auto* item) {
        if ((item->column() >= 2 && item->column() <= 4) || item->column() >= 7) invalidatePreview();
    });
    connect(iterations_, &QSpinBox::valueChanged, this, [this] { invalidatePreview(); });
    connect(grid_, &QSpinBox::valueChanged, this, [this] { invalidatePreview(); });
    connect(survival_, &QDoubleSpinBox::valueChanged, this, [this] { invalidatePreview(); });
    connect(refocus_, &QCheckBox::toggled, this, [this] { invalidatePreview(); });
}
void OptimizationEditor::appendVariable(OptimizationVariable v) {
    const int row = variables_->rowCount();
    variables_->insertRow(row);
    auto* surface = new QComboBox;
    for (size_t i = 0; i < project_.system.surfaces.size(); ++i)
        surface->addItem(QString("%1 — %2").arg(i + 1).arg(QString::fromStdString(project_.system.surfaces[i].name)));
    surface->setCurrentIndex(int(v.surface));
    auto* parameter = new QComboBox;
    for (int i = 0; i <= int(VariableParameter::A21); ++i)
        parameter->addItem(parameterName(VariableParameter(i)));
    parameter->setCurrentIndex(int(v.parameter));
    surface->setEnabled(v.parameter != VariableParameter::Defocus);
    variables_->setCellWidget(row, 0, surface);
    variables_->setCellWidget(row, 1, parameter);
    cell(variables_, row, 2, variableValue(project_.system, v), false);
    cell(variables_, row, 3, v.lower);
    cell(variables_, row, 4, v.upper);
    cell(variables_, row, 5, v.step);
    auto refresh = [this, surface, parameter] {
        // Locate by widget, since preceding rows may have been removed.
        int row = -1;
        for (int i = 0; i < variables_->rowCount(); ++i)
            if (variables_->cellWidget(i, 1) == parameter) row = i;
        if (row < 0 || surface->currentIndex() < 0) return;
        OptimizationVariable v;
        v.parameter = VariableParameter(parameter->currentIndex());
        v.surface = size_t(surface->currentIndex());
        surface->setEnabled(v.parameter != VariableParameter::Defocus);
        double value = variableValue(project_.system, v), range = std::max(1., std::abs(value) * .3);
        if (const int order = asphericOrder(v.parameter)) {
            const double aperture = std::max(1., project_.system.surfaces[v.surface].semiDiameter);
            range = std::max(std::abs(value) * .3, .1 / std::pow(aperture, order));
        }
        cell(variables_, row, 2, value, false);
        cell(variables_, row, 3, v.parameter == VariableParameter::Thickness ? std::max(0., value - range) : value - range);
        cell(variables_, row, 4, value + range);
        cell(variables_, row, 5, range / 5);
    };
    connect(surface, &QComboBox::currentIndexChanged, this, refresh);
    connect(parameter, &QComboBox::currentIndexChanged, this, refresh);
}
void OptimizationEditor::appendOperand(MeritOperand o) {
    int row = operands_->rowCount();
    operands_->insertRow(row);
    auto* kind = new QComboBox;
    for (int i = 0; i <= int(MeritKind::EdgeThickness); ++i) {
        kind->addItem(MeritKind(i)==MeritKind::TotalTrack ? "Длина системы, мм" : meritName(MeritKind(i)));
        kind->setItemData(i, meritName(MeritKind(i)), Qt::ToolTipRole);
    }
    kind->setCurrentIndex(int(o.kind));
    auto* field = new QComboBox;
    field->addItem("Все поля");
    for (size_t i = 0; i < project_.system.fields.size(); ++i) {
        auto f = project_.system.fields[i];
        field->addItem(QString("%1: %2 %4, %3 %4").arg(i + 1).arg(f.x).arg(f.y).arg(QString::fromUtf8(fieldUnit(project_.system.fieldType))));
    }
    field->setCurrentIndex(o.field + 1);
    auto enableField = [kind, field] {
        auto k = MeritKind(kind->currentIndex());
        field->setEnabled(k != MeritKind::EFL && k != MeritKind::BFL && k != MeritKind::ImageDistance && k != MeritKind::TotalTrack && k != MeritKind::EdgeThickness);
    };
    enableField();
    connect(kind, &QComboBox::currentIndexChanged, this, enableField);
    connect(kind, &QComboBox::currentIndexChanged, this, [this] { invalidatePreview(); });
    connect(field, &QComboBox::currentIndexChanged, this, [this] { invalidatePreview(); });
    operands_->setCellWidget(row, 0, kind);
    operands_->setCellWidget(row, 1, field);
    cell(operands_, row, 2, o.target);
    cell(operands_, row, 3, o.scale);
    cell(operands_, row, 4, o.weight);
    cell(operands_, row, 7, o.frequency);cell(operands_, row, 8, double(o.surface+1));cell(operands_, row, 9, o.height);
    auto enableParameters=[this,kind] {
        // Find the row by its live widget, since rows may be deleted.
        int current=-1;for(int r=0;r<operands_->rowCount();++r)if(operands_->cellWidget(r,0)==kind)current=r;
        if(current<0)return;
        const auto k=MeritKind(kind->currentIndex());
        for(int column:{7,8,9}) {
            auto* item=operands_->item(current,column);
            const bool enabled=column==7 ? k==MeritKind::MTFX || k==MeritKind::MTFY : k==MeritKind::EdgeThickness;
            auto flags=item->flags();if(enabled)flags|=Qt::ItemIsEditable;else flags&=~Qt::ItemIsEditable;
            item->setFlags(flags);item->setForeground(enabled ? QColor("#20364d") : QColor("#687b8e"));
        }
    };
    enableParameters();connect(kind,&QComboBox::currentIndexChanged,this,enableParameters);
    for (int c : {5, 6}) {
        cell(operands_, row, c, 0, false);
        operands_->item(row, c)->setText("—");
    }
}
OptimizationPlan OptimizationEditor::plan() const {
    OptimizationPlan p;
    p.iterations = size_t(iterations_->value());
    p.pupilGrid = grid_->value();
    p.minimumThroughput = survival_->value();
    p.refocus = refocus_->isChecked();
    p.algorithm=OptimizationAlgorithm(algorithm_->currentIndex());p.starts=size_t(starts_->value());
    p.allFieldsFocus=allFieldsFocus_->isChecked();
    for (int r = 0; r < variables_->rowCount(); ++r) {
        auto* parameter = qobject_cast<QComboBox*>(variables_->cellWidget(r, 1));
        auto* surface = qobject_cast<QComboBox*>(variables_->cellWidget(r, 0));
        auto k = VariableParameter(parameter->currentIndex());
        p.variables.push_back({k, k == VariableParameter::Defocus ? 0 : size_t(surface->currentIndex()),
                               readCell(variables_, r, 3), readCell(variables_, r, 4), readCell(variables_, r, 5)});
    }
    for (int r = 0; r < operands_->rowCount(); ++r) {
        auto* kind = qobject_cast<QComboBox*>(operands_->cellWidget(r, 0));
        auto* field = qobject_cast<QComboBox*>(operands_->cellWidget(r, 1));
        p.operands.push_back({MeritKind(kind->currentIndex()), field->isEnabled() ? field->currentIndex() - 1 : -1,
                              readCell(operands_, r, 2), readCell(operands_, r, 3), readCell(operands_, r, 4)});
        auto& operand=p.operands.back();
        if(operand.kind==MeritKind::MTFX || operand.kind==MeritKind::MTFY)operand.frequency=readCell(operands_,r,7);
        if(operand.kind==MeritKind::EdgeThickness) {
            const double surface=readCell(operands_,r,8);
            if(surface<1 || surface>double(project_.system.surfaces.size()-1) || std::floor(surface)!=surface)
                throw std::invalid_argument("Для края выберите номер поверхности перед промежутком");
            operand.surface=size_t(surface)-1;operand.height=readCell(operands_,r,9);
        }
    }
    return p;
}
void OptimizationEditor::preview() {
    for (int r = 0; r < operands_->rowCount(); ++r)
        for (int c : {5, 6}) operands_->item(r, c)->setText("—");
    try {
        auto p = plan();
        auto system = project_.system;
        auto errors = p.validate(system);
        if (errors.empty()) errors = system.validate(project_.catalog);
        if (!errors.empty()) throw std::invalid_argument(errors.front());
        if (p.refocus) {
            if(p.allFieldsFocus)autofocusAllFields(system,project_.catalog,p.pupilGrid);
            else autofocus(system,project_.catalog);
        }
        auto result = evaluateMerit(system, project_.catalog, p);
        for (int r = 0; r < operands_->rowCount(); ++r) {
            cell(operands_, r, 5, result.values[r], false);
            cell(operands_, r, 6, result.contributions[r], false);
        }
        message_->setText(QString("Критерий: %1 (безразмерный). Значения рассчитаны для сетки %2 × %2.")
                            .arg(result.score, 0, 'g', 8).arg(p.pupilGrid));
    } catch (const std::exception& e) {
        message_->setText(QString::fromUtf8(e.what()));
    }
}
void OptimizationEditor::invalidatePreview() {
    if (!message_) return;
    QSignalBlocker block(operands_);
    for (int row = 0; row < operands_->rowCount(); ++row)
        for (int col : {5, 6})
            if (auto* item = operands_->item(row, col)) item->setText("—");
    message_->setText("Настройки изменены. Рассчитайте критерий заново.");
}
