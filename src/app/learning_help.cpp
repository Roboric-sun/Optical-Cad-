#include "learning_help.hpp"
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTextBrowser>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

void showLearningGuide(QWidget* parent) {
    if (auto* existing = parent->findChild<QDialog*>("learningGuide")) {
        existing->show(); existing->raise(); existing->activateWindow();
        return;
    }
    QFile file(":/learning/LEARNING_CPP_RU.md");
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent, "Учебное руководство", "Не удалось прочитать встроенное руководство.");
        return;
    }
    auto* dialog = new QDialog(parent);
    dialog->setObjectName("learningGuide");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Как устроен Optical CAD — руководство по C++");
    dialog->resize(950, 740);
    auto* layout = new QVBoxLayout(dialog);
    auto* searchRow = new QHBoxLayout;
    auto* search = new QLineEdit(dialog);
    search->setObjectName("learningSearch");
    search->setPlaceholderText("Найти в руководстве…");
    auto* next = new QPushButton("Найти далее", dialog);
    next->setObjectName("learningFindNext");
    searchRow->addWidget(search); searchRow->addWidget(next);
    layout->addLayout(searchRow);
    auto* browser = new QTextBrowser(dialog);
    browser->setObjectName("learningText");
    browser->setMarkdown(QString::fromUtf8(file.readAll()));
    browser->setOpenLinks(false);
    QObject::connect(browser, &QTextBrowser::anchorClicked, dialog, [browser](const QUrl& url) {
        if (url.toString().startsWith('#')) browser->scrollToAnchor(url.fragment());
        else QDesktopServices::openUrl(QUrl("https://github.com/Roboric-sun/Optical-Cad-/blob/main/docs/").resolved(url));
    });
    layout->addWidget(browser);
    auto* status = new QLabel(dialog);
    status->setObjectName("learningSearchStatus");
    layout->addWidget(status);
    auto find = [search, browser, status] {
        status->clear();
        if (search->text().isEmpty()) return;
        if (!browser->find(search->text())) {
            browser->moveCursor(QTextCursor::Start);
            if (!browser->find(search->text())) status->setText("Текст не найден");
        }
    };
    QObject::connect(next, &QPushButton::clicked, dialog, find);
    QObject::connect(search, &QLineEdit::returnPressed, dialog, find);
    auto* shortcut = new QShortcut(QKeySequence::Find, dialog);
    QObject::connect(shortcut, &QShortcut::activated, dialog, [search] { search->setFocus(); search->selectAll(); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText("Закрыть");
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    dialog->show();
}
