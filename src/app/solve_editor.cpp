#include "solve_editor.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <algorithm>

using namespace optics;
QString solveDescription(const ParameterSolve& a) {
    if (a.kind == SolveKind::Pickup)
        return QString("Связь: %1 × поверхность %2 + %3 мм")
            .arg(a.scale).arg(a.reference + 1).arg(a.offset);
    if (a.kind == SolveKind::EdgeThickness)
        return QString("Край %1 мм на высоте %2 мм").arg(a.value).arg(a.height);
    return QString("Длина %1 мм от поверхности %2 до %3")
        .arg(a.value).arg(a.first + 1).arg(a.last + 1);
}
SolveEditor::SolveEditor(const Project& project, size_t surface, SolveParameter parameter, QWidget* parent)
    : QDialog(parent), result_(project.system) {
    setObjectName("solveDialog");
    setWindowTitle(QString("Поверхность %1 — %2").arg(surface + 1)
        .arg(parameter == SolveParameter::Radius ? "связь радиуса" : "расчёт толщины"));
    resize(560, 370);
    auto* form = new QFormLayout(this);
    auto* info = new QLabel("Связанный параметр пересчитывается при изменении источника. "
                           "Для ручного редактирования выберите «Фиксированное значение».");
    info->setWordWrap(true);
    form->addRow(info);
    auto* type = new QComboBox;
    type->setObjectName("solveType");
    type->addItems({"Фиксированное значение", "Связь с другой поверхностью"});
    if (parameter == SolveParameter::Thickness)
        type->addItems({"Краевая толщина", "Общая длина между поверхностями"});
    form->addRow("Способ задания", type);
    auto spin = [&](QString id, double initial, double low, double high) {
        auto* box = new QDoubleSpinBox;
        box->setObjectName(id);
        box->setDecimals(8);
        box->setRange(low, high);
        box->setValue(initial);
        return box;
    };
    auto* reference = new QComboBox;
    reference->setObjectName("solveReference");
    auto* first = new QComboBox;
    first->setObjectName("solveFirst");
    auto* last = new QComboBox;
    last->setObjectName("solveLast");
    for (size_t i = 0; i < result_.surfaces.size(); ++i) {
        const auto text = QString("%1 · %2").arg(i + 1).arg(QString::fromStdString(result_.surfaces[i].name));
        reference->addItem(text);
        first->addItem(text);
        last->addItem(text);
    }
    last->addItem(QString("%1 · Изображение").arg(result_.surfaces.size() + 1));
    reference->setCurrentIndex(surface ? 0 : std::min(size_t(1), result_.surfaces.size() - 1));
    last->setCurrentIndex(int(result_.surfaces.size()));
    auto* scale = spin("solveScale", 1, -1e6, 1e6);
    auto* offset = spin("solveOffset", 0, -1e6, 1e6);
    auto* value = spin("solveValue", parameter == SolveParameter::Thickness ? result_.surfaces[surface].thickness : 0, 0, 1e6);
    auto* height = spin("solveHeight", 0, 0, 1e6);
    form->addRow("Поверхность-источник", reference);
    form->addRow("Множитель", scale);
    form->addRow("Смещение, мм", offset);
    form->addRow("Заданная толщина / длина, мм", value);
    form->addRow("Радиальная высота края, мм", height);
    form->addRow("Начальная поверхность", first);
    form->addRow("Конечная поверхность", last);
    auto* message = new QLabel;
    message->setObjectName("solveMessage");
    message->setWordWrap(true);
    message->setStyleSheet("color:#ac3030");
    form->addRow(message);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("ОК");
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    form->addRow(buttons);
    for (const auto& a : result_.solves)
        if (a.surface == surface && a.parameter == parameter) {
            type->setCurrentIndex(int(a.kind) + 1);
            reference->setCurrentIndex(int(a.reference));
            scale->setValue(a.scale); offset->setValue(a.offset);
            value->setValue(a.value); height->setValue(a.height);
            first->setCurrentIndex(int(a.first)); last->setCurrentIndex(int(a.last));
        }
    auto update = [=, this] {
        const int t = type->currentIndex();
        for (auto* widget : std::initializer_list<QWidget*>{reference, scale, offset, value, height, first, last}) {
            bool visible = t == 1 ? widget == reference || widget == scale || widget == offset :
                           t == 2 ? widget == value || widget == height :
                           t == 3 ? widget == value || widget == first || widget == last : false;
            form->setRowVisible(widget, visible);
        }
        message->clear();
        form->activate();
        resize(std::max(560, sizeHint().width()), sizeHint().height());
    };
    connect(type, &QComboBox::currentIndexChanged, this, update);
    update();
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this] {
        try {
            auto candidate = project.system;
            std::erase_if(candidate.solves, [&](const auto& a) { return a.surface == surface && a.parameter == parameter; });
            if (type->currentIndex()) {
                ParameterSolve a;
                a.parameter = parameter; a.surface = surface;
                a.kind = SolveKind(type->currentIndex() - 1);
                a.reference = size_t(reference->currentIndex());
                if (a.kind == SolveKind::EdgeThickness) a.reference = surface + 1;
                a.scale = scale->value(); a.offset = offset->value();
                a.value = value->value(); a.height = height->value();
                a.first = size_t(first->currentIndex()); a.last = size_t(last->currentIndex());
                candidate.solves.push_back(a);
            }
            applySolves(candidate);
            auto errors = candidate.validate(project.catalog);
            if (errors.empty() && project.optimization) errors = project.optimization->validate(candidate);
            if (!errors.empty()) throw std::invalid_argument(errors.front());
            result_ = std::move(candidate);
            accept();
        } catch (const std::exception& e) { message->setText(QString::fromUtf8(e.what())); }
    });
}
