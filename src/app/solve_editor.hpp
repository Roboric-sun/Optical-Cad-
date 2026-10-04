#pragma once
#include "project.hpp"
#include <QDialog>

QString solveDescription(const optics::ParameterSolve&);
class SolveEditor : public QDialog {
    Q_OBJECT
  public:
    SolveEditor(const Project&, size_t surface, optics::SolveParameter, QWidget* parent = nullptr);
    const optics::SequentialSystem& system() const { return result_; }
  private:
    optics::SequentialSystem result_;
};
