#pragma once
#include "project.hpp"
#include <QDialog>
class QTableWidget;
class QLabel;
class QPlainTextEdit;
class CoordinateEditor:public QDialog {
public:
    CoordinateEditor(const Project&,QWidget* parent=nullptr);
    optics::SequentialSystem system() const;
private:
    Project project_;
    QTableWidget* table_;
    QLabel* message_;
    QPlainTextEdit* frames_;
    void preview();
};
