#pragma once
#include "project.hpp"
#include <QDialog>
class QComboBox;class QDoubleSpinBox;
class ShapeEditor:public QDialog {
public:
 ShapeEditor(const Project&,size_t surface,QWidget* parent=nullptr);
 optics::Surface surface()const;
private:
 Project project_;size_t index_;
 QComboBox *profile_,*aperture_;
 QDoubleSpinBox *radius_,*rotation_,*semi_,*x_,*y_,*obscuration_;
};
