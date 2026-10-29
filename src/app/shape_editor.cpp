#include "shape_editor.hpp"
#include "number_input.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
using namespace optics;
ShapeEditor::ShapeEditor(const Project& p,size_t index,QWidget* parent):QDialog(parent),project_(p),index_(index) {
 setObjectName("shapeDialog");setWindowTitle(QString("Профиль и апертура · S%1").arg(index+1));resize(620,500);
 const auto& s=p.system.surfaces.at(index);auto* layout=new QFormLayout(this);
 auto spin=[&](const char* name,double value,double lo,double hi){auto* box=new DecimalSpinBox;box->setObjectName(name);box->setDecimals(9);box->setRange(lo,hi);box->setValue(value);return box;};
 profile_=new QComboBox;profile_->setObjectName("surfaceProfile");profile_->addItems({"Стандартная осесимметричная","Цилиндр · кривизна по X","Цилиндр · кривизна по Y","Тороидальная"});profile_->setCurrentIndex(int(s.profile));layout->addRow("Профиль",profile_);
 radius_=spin("profileRadius",s.radius,-1e6,1e6);rotation_=spin("rotationRadius",s.rotationRadius,-1e6,1e6);
 layout->addRow("Радиус, мм (у тора Ry)",radius_);layout->addRow("Радиус вращения тора Rx, мм",rotation_);
 semi_=spin("mechanicalSemiDiameter",s.semiDiameter,.000001,1e5);layout->addRow("Механический полудиаметр, мм",semi_);
 aperture_=new QComboBox;aperture_->setObjectName("surfaceAperture");aperture_->addItems({"Круг","Эллипс","Прямоугольник"});aperture_->setCurrentIndex(int(s.aperture.shape));layout->addRow("Световая апертура",aperture_);
 x_=spin("apertureHalfX",s.aperture.halfX,.000001,1e5);y_=spin("apertureHalfY",s.aperture.halfY,.000001,1e5);obscuration_=spin("apertureObscuration",s.aperture.obscuration,0,1e5);
 layout->addRow("Полуразмер X, мм",x_);layout->addRow("Полуразмер Y, мм",y_);layout->addRow("Радиус центрального экрана, мм",obscuration_);
 auto* note=new QLabel("0 радиуса означает плоское направление. Маска помещается внутри механического круга. Для новых профилей доступны геометрические анализы и первый порядок 4×4; волновые расчёты пока не аттестованы.");note->setWordWrap(true);layout->addRow(note);
 auto* message=new QLabel;message->setObjectName("shapeMessage");message->setWordWrap(true);layout->addRow(message);
 auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setObjectName("shapeSave");buttons->button(QDialogButtonBox::Save)->setText("Сохранить");buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");layout->addRow(buttons);
 auto enable=[&]{rotation_->setEnabled(profile_->currentIndex()==3);x_->setEnabled(aperture_->currentIndex()!=0);y_->setEnabled(aperture_->currentIndex()!=0);};enable();
 connect(profile_,&QComboBox::currentIndexChanged,this,[this]{rotation_->setEnabled(profile_->currentIndex()==3);});
 connect(aperture_,&QComboBox::currentIndexChanged,this,[this]{x_->setEnabled(aperture_->currentIndex()!=0);y_->setEnabled(aperture_->currentIndex()!=0);});
 for(auto* box:{radius_,rotation_,semi_,x_,y_,obscuration_})connect(box,&QDoubleSpinBox::valueChanged,message,[message]{message->clear();});
 connect(profile_,&QComboBox::currentIndexChanged,message,[message]{message->clear();});connect(aperture_,&QComboBox::currentIndexChanged,message,[message]{message->clear();});
 connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
 connect(buttons,&QDialogButtonBox::accepted,this,[this,message]{try{auto p=project_;p.system.surfaces[index_]=surface();auto errors=p.system.validate(p.catalog);if(errors.empty()&&p.optimization)errors=p.optimization->validate(p.system);if(!errors.empty())throw std::invalid_argument(errors.front());accept();}catch(const std::exception& e){message->setText(QString::fromUtf8(e.what()));}});
}
Surface ShapeEditor::surface()const {
 auto s=project_.system.surfaces.at(index_);s.profile=SurfaceProfile(profile_->currentIndex());s.radius=radius_->value();s.rotationRadius=s.profile==SurfaceProfile::Toroidal?rotation_->value():0;
 s.semiDiameter=semi_->value();s.aperture.shape=ApertureShape(aperture_->currentIndex());s.aperture.halfX=x_->value();s.aperture.halfY=y_->value();s.aperture.obscuration=obscuration_->value();return s;
}
