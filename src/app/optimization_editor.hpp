#pragma once
#include "project.hpp"
#include <QDialog>

class QTableWidget;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLabel;

QString meritName(optics::MeritKind);
QString parameterName(optics::VariableParameter);
class OptimizationEditor : public QDialog {
  public:
    OptimizationEditor(const Project&, QWidget* parent = nullptr);
    optics::OptimizationPlan plan() const;
  private:
    Project project_;
    QTableWidget *variables_, *operands_;
    QSpinBox *iterations_, *grid_;
    QDoubleSpinBox* survival_;
    QCheckBox* refocus_;
    QLabel* message_ = nullptr;
    void appendVariable(optics::OptimizationVariable);
    void appendOperand(optics::MeritOperand);
    void preview();
    void invalidatePreview();
};
