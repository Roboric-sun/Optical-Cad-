#include "field_editor.hpp"
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <memory>

using namespace optics;
FieldEditor::FieldEditor(const Project& project, QWidget* parent) : QDialog(parent), result_(project) {
    setObjectName("fieldDialog");
    setWindowTitle("Поля зрения и виньетирование");
    resize(820, 410);
    auto* layout = new QVBoxLayout(this);
    auto* info = new QLabel("X/Y — углы объекта в градусах. VUX/VLX — положительная/отрицательная сторона X зрачка; "
                           "VUY/VLY — стороны Y. Коэффициент 0 оставляет сторону целиком; 0,4 сжимает её до 60%. "
                           "Допустимо 0 ≤ V < 1. Главный луч остаётся в центре.");
    info->setWordWrap(true); layout->addWidget(info);
    auto* table = new QTableWidget;
    table->setObjectName("fieldTable");
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"X, °", "Y, °", "Вес", "VUX (+X)", "VLX (−X)", "VUY (+Y)", "VLY (−Y)"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table);
    // Identity is independent of editable coordinates and survives row movement.
    auto identities = std::make_shared<std::vector<size_t>>();
    auto append = [table, identities](Field f, size_t identity) {
        const int row = table->rowCount(); table->insertRow(row);
        const double values[] = {f.x, f.y, f.weight, f.vux, f.vlx, f.vuy, f.vly};
        for (int col = 0; col < 7; ++col)
            table->setItem(row, col, new QTableWidgetItem(QString::number(values[col], 'g', 17)));
        identities->push_back(identity);
        table->setCurrentCell(row, 0);
    };
    for (size_t i = 0; i < project.system.fields.size(); ++i) append(project.system.fields[i], i);
    table->setCurrentCell(0, 0);
    auto* rowButtons = new QHBoxLayout;
    auto button = [&](const char* name, const char* title) {
        auto* b = new QPushButton(QString::fromUtf8(title)); b->setObjectName(name);
        rowButtons->addWidget(b); return b;
    };
    auto* add = button("fieldAdd", "Добавить");
    auto* remove = button("fieldRemove", "Удалить");
    auto* up = button("fieldUp", "Вверх");
    auto* down = button("fieldDown", "Вниз");
    rowButtons->addStretch(); layout->addLayout(rowButtons);
    auto* message = new QLabel;
    message->setObjectName("fieldMessage"); message->setWordWrap(true);
    message->setStyleSheet("color:#ac3030"); layout->addWidget(message);
    connect(add, &QPushButton::clicked, this, [=] {
        if (table->rowCount() >= 50) { message->setText("Допустимо не более 50 полей"); return; }
        append(Field{}, SIZE_MAX); message->clear();
    });
    connect(remove, &QPushButton::clicked, this, [=] {
        const int row = table->currentRow();
        if (table->rowCount() <= 1) { message->setText("В системе должно остаться хотя бы одно поле"); return; }
        if (row < 0) return;
        table->removeRow(row); identities->erase(identities->begin() + row);
        table->setCurrentCell(std::min(row, table->rowCount() - 1), 0); message->clear();
    });
    auto move = [=](int step) {
        const int row = table->currentRow(), to = row + step;
        if (row < 0 || to < 0 || to >= table->rowCount()) return;
        for (int col = 0; col < 7; ++col) {
            auto* a = table->takeItem(row, col); auto* b = table->takeItem(to, col);
            table->setItem(row, col, b); table->setItem(to, col, a);
        }
        std::swap((*identities)[row], (*identities)[to]);
        table->setCurrentCell(to, 0); message->clear();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("ОК");
    buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [=, this] {
        try {
            auto candidate = project;
            candidate.system.fields.clear();
            std::vector<size_t> map(project.system.fields.size(), SIZE_MAX);
            for (int row = 0; row < table->rowCount(); ++row) {
                std::array<double, 7> values{};
                for (int col = 0; col < 7; ++col) {
                    bool ok = false;
                    auto* item = table->item(row, col);
                    values[col] = item ? item->text().trimmed().replace(',', '.').toDouble(&ok) : 0;
                    if (!ok || !std::isfinite(values[col]))
                        throw std::invalid_argument("В каждой ячейке нужно конечное число");
                }
                Field f{values[0], values[1], values[2], values[3], values[4], values[5], values[6]};
                if (std::abs(f.x) > 80 || std::abs(f.y) > 80 || f.weight <= 0 || !validVignetting(f))
                    throw std::invalid_argument("Поля: |X/Y| ≤ 80°, вес > 0, коэффициенты 0 ≤ V < 1");
                candidate.system.fields.push_back(f);
                if ((*identities)[row] != SIZE_MAX) map.at((*identities)[row]) = size_t(row);
            }
            auto mapped = [&](size_t field) {
                if (field >= map.size() || map[field] == SIZE_MAX)
                    throw std::invalid_argument("Удаляемое поле используется решением по лучу или функцией качества: сначала измените эту ссылку");
                return map[field];
            };
            for (auto& solve : candidate.system.solves)
                if (solve.kind == SolveKind::MarginalHeight) solve.field = mapped(solve.field);
            if (candidate.optimization)
                for (auto& operand : candidate.optimization->operands)
                    if (operand.field >= 0) operand.field = int(mapped(size_t(operand.field)));
            applySolves(candidate.system, candidate.catalog);
            auto errors = candidate.system.validate(candidate.catalog);
            if (errors.empty() && candidate.optimization) errors = candidate.optimization->validate(candidate.system);
            if (!errors.empty()) throw std::invalid_argument(errors.front());
            result_ = std::move(candidate); mapping_ = std::move(map); accept();
        } catch (const std::exception& e) { message->setText(QString::fromUtf8(e.what())); }
    });
}
