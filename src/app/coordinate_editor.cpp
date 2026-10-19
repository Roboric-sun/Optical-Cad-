#include "coordinate_editor.hpp"
#include "number_input.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
using namespace optics;
CoordinateEditor::CoordinateEditor(const Project& p,QWidget* parent):QDialog(parent),project_(p) {
    setObjectName("coordinateDialog");setWindowTitle("Локальные системы координат");resize(1180,680);
    auto* layout=new QVBoxLayout(this);
    auto* help=new QLabel("Преобразование ПЕРЕД поверхностью наследуется следующими. Возврат: глобальная или предыдущая номинальная система. Обычные наклоны поверхности остаются отдельными. Диаметр зрачка в этом режиме задаёт физический диаметр STOP.");
    help->setWordWrap(true);layout->addWidget(help);
    table_=new QTableWidget(int(p.system.surfaces.size()),10);table_->setObjectName("coordinateTable");
    table_->setHorizontalHeaderLabels({"Поверхность","Режим","Порядок","Возврат к","X, мм","Y, мм","Z, мм","Rx, °","Ry, °","Rz, °"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for(int col:{0,1,2,3}){table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Interactive);table_->setColumnWidth(col,col==0?170:170);}
    table_->setAlternatingRowColors(true);layout->addWidget(table_,1);
    for(int r=0;r<table_->rowCount();++r) {
        const auto& surface=p.system.surfaces[size_t(r)];const auto& a=surface.coordinates;
        auto* name=new QTableWidgetItem(QString("%1 · %2").arg(r+1).arg(QString::fromStdString(surface.name)));name->setFlags(name->flags()&~Qt::ItemIsEditable);table_->setItem(r,0,name);
        auto* mode=new QComboBox;mode->addItems({"Без преобразования","Относительное","Возврат ориентации","Полный возврат"});mode->setCurrentIndex(int(a.mode));table_->setCellWidget(r,1,mode);
        auto* order=new QComboBox;order->addItems({"Перенос → Наклон","Наклон → Перенос"});order->setCurrentIndex(int(a.order));table_->setCellWidget(r,2,order);
        auto* reference=new QComboBox;reference->addItem("Глобальная",-1);
        for(int i=0;i<r;++i)reference->addItem(QString("%1 · %2").arg(i+1).arg(QString::fromStdString(p.system.surfaces[size_t(i)].name)),i);
        reference->setCurrentIndex(a.reference+1);table_->setCellWidget(r,3,reference);
        const double values[]{a.shift.x,a.shift.y,a.shift.z,a.tilt.x,a.tilt.y,a.tilt.z};
        for(int i=0;i<6;++i)table_->setItem(r,i+4,new QTableWidgetItem(QString::number(values[i],'g',12)));
        auto enable=[mode,order,reference]{order->setEnabled(mode->currentIndex()!=0);reference->setEnabled(mode->currentIndex()>=2);};
        enable();connect(mode,&QComboBox::currentIndexChanged,this,enable);
    }
    message_=new QLabel("Сначала проверьте глобальные координаты; сохранение применяется одним действием undo.");message_->setWordWrap(true);message_->setObjectName("coordinateMessage");layout->addWidget(message_);
    frames_=new QPlainTextEdit;frames_->setObjectName("coordinateFrames");frames_->setReadOnly(true);frames_->setMaximumHeight(180);layout->addWidget(frames_);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
    auto* calculate=buttons->addButton("Проверить координаты",QDialogButtonBox::ActionRole);calculate->setObjectName("coordinatePreview");
    buttons->button(QDialogButtonBox::Save)->setObjectName("coordinateSave");buttons->button(QDialogButtonBox::Save)->setText("Сохранить");buttons->button(QDialogButtonBox::Cancel)->setText("Отмена");layout->addWidget(buttons);
    connect(calculate,&QPushButton::clicked,this,[this]{preview();});connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{try{auto s=system();auto errors=s.validate(project_.catalog);if(errors.empty() && project_.optimization)errors=project_.optimization->validate(s);if(!errors.empty())throw std::invalid_argument(errors.front());accept();}catch(const std::exception& e){message_->setText(QString::fromUtf8(e.what()));}});
}
SequentialSystem CoordinateEditor::system() const {
    auto s=project_.system;
    for(int r=0;r<table_->rowCount();++r) {
        auto& a=s.surfaces[size_t(r)].coordinates;a={};
        a.mode=CoordinateMode(qobject_cast<QComboBox*>(table_->cellWidget(r,1))->currentIndex());
        if(a.mode==CoordinateMode::None)continue;
        a.order=CoordinateOrder(qobject_cast<QComboBox*>(table_->cellWidget(r,2))->currentIndex());
        if(a.mode==CoordinateMode::ReturnFrame || a.mode==CoordinateMode::ReturnOrientation)
            a.reference=qobject_cast<QComboBox*>(table_->cellWidget(r,3))->currentData().toInt();
        double values[6];for(int i=0;i<6;++i){bool ok=false;values[i]=uiNumber(table_->item(r,i+4)->text(),&ok);if(!ok)throw std::invalid_argument("Нужно конечное число; допустимы точка и запятая");}
        a.shift={values[0],values[1],values[2]};a.tilt={values[3],values[4],values[5]};
    }
    applySolves(s,project_.catalog);return s;
}
void CoordinateEditor::preview() {
    try {
        const auto s=system();const auto frames=sequentialFrames(s,project_.catalog);QString text;
        for(size_t i=0;i<frames.surfaces.size();++i){const auto& f=frames.surfaces[i];const auto z=f.basis[2];text+=QString("S%1: O=(%2; %3; %4), Z=(%5; %6; %7)\n").arg(i+1).arg(f.position.x,0,'g',10).arg(f.position.y,0,'g',10).arg(f.position.z,0,'g',10).arg(z.x,0,'g',10).arg(z.y,0,'g',10).arg(z.z,0,'g',10);}
        const auto p=frames.image.position;text+=QString("Изображение: O=(%1; %2; %3) мм").arg(p.x).arg(p.y).arg(p.z);frames_->setPlainText(text);message_->setText("Координаты проверены. Z — единичное направление локальной оптической оси.");
    }catch(const std::exception& e){frames_->clear();message_->setText(QString::fromUtf8(e.what()));}
}
