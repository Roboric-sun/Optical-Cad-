#pragma once
#include "project.hpp"
#include <QDialog>
#include <QFutureWatcher>
#include <atomic>
class QComboBox;class QLineEdit;class QDoubleSpinBox;class QSpinBox;class QTableWidget;class QPlainTextEdit;class QLabel;class QTabWidget;class QPushButton;class QProgressBar;class QTimer;
struct StudyOutcome {Project project;QString text,error;std::optional<optics::ToleranceReport> report;bool cancelled=false;};
class StudyEditor:public QDialog {
 public:
    StudyEditor(const Project&,bool toleranceTab=false,QWidget* parent=nullptr);
    ~StudyEditor() override;
    const Project& project()const{return project_;}
    bool busy()const{return busy_;}
 public:void reject()override;
 private:
    Project project_,reportProject_;
    int selected_=-1;bool busy_=false,closing_=false;
    QComboBox* configs_;QLineEdit *name_,*seed_;QDoubleSpinBox *weight_,*limit_;QSpinBox *samples_,*iterations_,*sampleIndex_,*stateIndex_;
    QTableWidget *values_,*materials_,*tolerances_,*compensators_,*correlation_;
    QPlainTextEdit* reportText_;QLabel* message_;QTabWidget* tabs_;QPushButton* save_;QProgressBar* progress_;QTimer* timer_;
    QFutureWatcher<StudyOutcome> watcher_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::shared_ptr<std::atomic<size_t>> done_,total_;
    std::optional<optics::ToleranceReport> report_;
    void loadConfigurations();void loadState(int);void storeState();void loadTolerance();Project read();
    void launch(int);void preview();void exportReport(bool csv);void exportSample();void guard(const std::function<void()>&);
};
