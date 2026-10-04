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
    const QStringList names{"Радиус, мм", "Толщина, мм", "Коника", "A4, мм⁻³", "A6, мм⁻⁵",
                            "A8, мм⁻⁷", "A10, мм⁻⁹", "Дефокус, мм"};
    return names.value(int(p));
}
QString meritName(MeritKind k) {
    const QStringList names{"RMS пятна, мм", "EFL, мм", "BFL, мм", "Изображение Z, мм",
                            "Центр X, мм", "Центр Y, мм", "Пропускание, 0…1"};
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
    double v = item ? item->text().toDouble(&valid) : 0;
    if (!valid || !std::isfinite(v))
        throw std::invalid_argument("В таблице нужно конечное число (десятичный разделитель — точка)");
    return v;
}
OptimizationEditor::OptimizationEditor(const Project& project, QWidget* parent)
    : QDialog(parent), project_(project) {
    setObjectName("optimizationDialog");
    setWindowTitle("Переменные и функция качества");
    resize(1020, 660);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        "Критерий = √(Σ вес × ((значение − цель) / масштаб)² / Σ вес). "
        "Для «Всех полей» квадраты отклонений усредняются с весами полей. "
        "RMS и центры учитывают все длины волн и их веса.");
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
        {"Критерий", "Поле", "Цель", "Масштаб", "Вес", "Значение", "Вклад"},
        operands_, "meritOperands");
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
    auto* controls = new QHBoxLayout;
    iterations_ = new QSpinBox;
    iterations_->setObjectName("optimizationIterations");
    iterations_->setRange(1, 100);
    iterations_->setValue(int(p.iterations));
    grid_ = new QSpinBox;
    grid_->setObjectName("optimizationGrid");
    grid_->setRange(3, 33);
    grid_->setValue(p.pupilGrid);
    survival_ = new QDoubleSpinBox;
    survival_->setObjectName("optimizationSurvival");
    survival_->setRange(0, 1);
    survival_->setDecimals(3);
    survival_->setSingleStep(.01);
    survival_->setValue(p.minimumThroughput);
    survival_->setToolTip("Минимальная доля дошедших лучей на каждом поле с весами волн, до потерь на покрытиях. 0 отключает ограничение.");
    refocus_ = new QCheckBox("Автофокус");
    refocus_->setObjectName("optimizationRefocus");
    refocus_->setChecked(p.refocus);
    refocus_->setToolTip("Минимум геометрического RMS первого поля на первичной волне. Последняя толщина и дефокус должны быть исключены из переменных.");
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
        if (item->column() >= 2 && item->column() <= 4) invalidatePreview();
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
    for (int i = 0; i <= int(VariableParameter::Defocus); ++i)
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
        if (v.parameter >= VariableParameter::A4 && v.parameter <= VariableParameter::A10) {
            const double aperture = std::max(1., project_.system.surfaces[v.surface].semiDiameter);
            range = std::max(std::abs(value) * .3, .1 / std::pow(aperture, 4 + 2 * (int(v.parameter) - int(VariableParameter::A4))));
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
    for (int i = 0; i <= int(MeritKind::Throughput); ++i) {
        kind->addItem(meritName(MeritKind(i)));
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
        field->setEnabled(k != MeritKind::EFL && k != MeritKind::BFL && k != MeritKind::ImageDistance);
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
        if (p.refocus) autofocus(system, project_.catalog);
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
