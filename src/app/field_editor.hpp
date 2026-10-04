#pragma once
#include "project.hpp"
#include <QDialog>

class FieldEditor : public QDialog {
    Q_OBJECT
  public:
    explicit FieldEditor(const Project&, QWidget* parent = nullptr);
    const Project& project() const { return result_; }
    const std::vector<size_t>& mapping() const { return mapping_; }
  private:
    Project result_;
    std::vector<size_t> mapping_;
};
