#pragma once
#include "project.hpp"
#include <QDialog>
#include <QProcess>
#include <QTemporaryDir>
#include <functional>
class QPlainTextEdit;
class QPushButton;
class QLineEdit;
class PythonConsole : public QDialog {
  public:
    PythonConsole(Project project, std::function<void(Project)> apply, QWidget* parent=nullptr);
    ~PythonConsole() override;
  private:
    Project project_;
    std::function<void(Project)> apply_;
    QTemporaryDir directory_;
    QProcess process_;
    QPlainTextEdit *source_, *output_;
    QPushButton *run_, *stop_;
    QLineEdit* interpreter_;
    bool stopped_=false;
    void* processJob_=nullptr;
    void stopProcess();
    void run();
};
