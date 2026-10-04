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
    if (a.kind == SolveKind::MarginalHeight)
        return QString("Высота луча %1 мм на следующей поверхности; поле %2, волна %3, зрачок Y=%4")
            .arg(a.value).arg(a.field + 1)
            .arg(a.wavelength == SIZE_MAX ? "первичная" : QString::number(a.wavelength + 1)).arg(a.pupil);
    if (a.kind == SolveKind::CurvaturePickup)
        return QString("Кривизна: %1 × (1/R поверхности %2) + %3 1/мм; плоскость = 0")
            .arg(a.scale).arg(a.reference + 1).arg(a.offset, 0, 'g', 12);
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
    type->addItem("Фиксированное значение", -1);
    type->addItem("Связь с другой поверхностью", int(SolveKind::Pickup));
    if (parameter == SolveParameter::Thickness) {
        type->addItem("Краевая толщина", int(SolveKind::EdgeThickness));
        type->addItem("Общая длина между поверхностями", int(SolveKind::OverallLength));
        type->addItem("Высота меридионального луча", int(SolveKind::MarginalHeight));
    } else {
        type->addItem("Связь кривизны (1/R)", int(SolveKind::CurvaturePickup));
    }
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
    offset->setDecimals(12);
    auto* value = spin("solveValue", parameter == SolveParameter::Thickness ? result_.surfaces[surface].thickness : 0, -1e6, 1e6);
    auto* height = spin("solveHeight", 0, 0, 1e6);
    auto* field = new QComboBox;
    field->setObjectName("solveField");
    for (size_t i = 0; i < result_.fields.size(); ++i)
        field->addItem(QString("%1 · X=%2 %4, Y=%3 %4").arg(i + 1).arg(result_.fields[i].x).arg(result_.fields[i].y).arg(QString::fromUtf8(fieldUnit(result_.fieldType))));
    auto* wave = new QComboBox;
    wave->setObjectName("solveWavelength");
    wave->addItem("Следовать первичной волне", -1);
    for (size_t i = 0; i < result_.wavelengths.size(); ++i)
        wave->addItem(QString("%1 · %2 мкм").arg(i + 1).arg(result_.wavelengths[i].um, 0, 'g', 10), int(i));
    auto* pupil = spin("solvePupil", 1, -1, 1);
    form->addRow("Поверхность-источник", reference);
    form->addRow("Множитель", scale);
    form->addRow("Смещение, мм", offset);
    form->addRow("Заданная толщина / длина, мм", value);
    form->addRow("Радиальная высота края, мм", height);
    form->addRow("Начальная поверхность", first);
    form->addRow("Конечная поверхность", last);
    form->addRow("Поле для расчёта", field);
    form->addRow("Длина волны", wave);
    form->addRow("Координата Y зрачка (−1…1)", pupil);
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
            type->setCurrentIndex(type->findData(int(a.kind)));
            reference->setCurrentIndex(int(a.reference));
            scale->setValue(a.scale); offset->setValue(a.offset);
            value->setValue(a.value); height->setValue(a.height);
            first->setCurrentIndex(int(a.first)); last->setCurrentIndex(int(a.last));
            field->setCurrentIndex(int(a.field));
            wave->setCurrentIndex(wave->findData(a.wavelength == SIZE_MAX ? -1 : int(a.wavelength)));
            pupil->setValue(a.pupil);
        }
    auto update = [=, this] {
        const int kind = type->currentData().toInt();
        const bool pickup = kind == int(SolveKind::Pickup) || kind == int(SolveKind::CurvaturePickup);
        const bool marginal = kind == int(SolveKind::MarginalHeight);
        value->setMinimum(marginal ? -1e6 : 0);
        if (auto* label = qobject_cast<QLabel*>(form->labelForField(value)))
            label->setText(marginal ? (surface + 1 == result_.surfaces.size()
                ? "Высота луча на изображении, мм" : "Высота луча на следующей поверхности, мм")
                : "Заданная толщина / длина, мм");
        if (auto* label = qobject_cast<QLabel*>(form->labelForField(offset)))
            label->setText(kind == int(SolveKind::CurvaturePickup) ? "Смещение кривизны, 1/мм" : "Смещение, мм");
        info->setText(marginal
            ? "Толщина рассчитывается по реальному лучу. X поля = 0; поверхности соосны, без зеркал; STOP расположен до управляемого промежутка. Высота знаковая, 0 задаёт пересечение оси. Y зрачка = ±1 — край, 0 — центр."
            : kind == int(SolveKind::CurvaturePickup)
            ? "Кривизна c = 1/R; для плоскости c = 0. Новая c = множитель × c источника + смещение. В таблице показан радиус в мм."
            : "Связанный параметр пересчитывается при изменении источника. Для ручного редактирования выберите «Фиксированное значение».");
        for (auto* widget : std::initializer_list<QWidget*>{reference, scale, offset, value, height, first, last, field, wave, pupil}) {
            bool visible = pickup ? widget == reference || widget == scale || widget == offset :
                           kind == int(SolveKind::EdgeThickness) ? widget == value || widget == height :
                           kind == int(SolveKind::OverallLength) ? widget == value || widget == first || widget == last :
                           marginal ? widget == value || widget == field || widget == wave || widget == pupil : false;
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
                a.kind = SolveKind(type->currentData().toInt());
                a.reference = size_t(reference->currentIndex());
                if (a.kind == SolveKind::EdgeThickness) a.reference = surface + 1;
                if (a.kind == SolveKind::MarginalHeight) a.reference = surface + 1;
                a.scale = scale->value(); a.offset = offset->value();
                a.value = value->value(); a.height = height->value();
                a.first = size_t(first->currentIndex()); a.last = size_t(last->currentIndex());
                a.field = size_t(field->currentIndex());
                a.wavelength = wave->currentData().toInt() == -1 ? SIZE_MAX : size_t(wave->currentData().toInt());
                a.pupil = pupil->value();
                candidate.solves.push_back(a);
            }
            applySolves(candidate, project.catalog);
            auto errors = candidate.validate(project.catalog);
            if (errors.empty() && project.optimization) errors = project.optimization->validate(candidate);
            if (!errors.empty()) throw std::invalid_argument(errors.front());
            result_ = std::move(candidate);
            accept();
        } catch (const std::exception& e) { message->setText(QString::fromUtf8(e.what())); }
    });
}
