#pragma once
#include "plots.hpp"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QSet>
#include <QTimer>
#include <atomic>

class QTreeWidget;
class QTableWidget;
class QTabWidget;
class QTabBar;
class QDockWidget;
class QComboBox;
class QProgressBar;
class QLabel;
class QToolBar;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class Window : public QMainWindow {
  public:
    explicit Window(QWidget* parent = nullptr);
    ~Window() override;
    const Project& project() const {
        return project_;
    }
    std::shared_ptr<Results> results() const {
        return results_;
    }
    void setProject(Project project);
    void recalculate();
    void runScene();
    bool calculating() const {
        return sceneBusy_;
    }
    void writeExamples(const QString& directory);

  protected:
    void closeEvent(QCloseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

  private:
    Project project_;
    std::shared_ptr<Results> results_;
    QTreeWidget* tree_;
    QTableWidget *surfaces_, *objects_, *sources_;
    QTabWidget *views_, *editors_;
    QTabBar* modes_;
    QWidget* ribbon_;
    QDockWidget *navigator_, *editorDock_, *detectorDock_;
    QWidget* editorPage_ = nullptr;
    PlotWidget* detectorPlot_;
    QToolBar* plotBar_;
    QLineEdit* treeSearch_;
    QDoubleSpinBox* scaleBox_;
    QSpinBox* displayRays_;
    QLabel *scaleLabel_, *displayLabel_;
    QSet<int> hidden_;
    bool uiReady_ = false;
    bool sceneBusy_ = false;
    bool advancedSurfaces_ = false;
    QComboBox *fieldBox_, *detectorBox_;
    QProgressBar* progress_;
    QLabel* status_;
    QTimer debounce_;
    QFutureWatcher<optics::SceneTrace> watcher_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    size_t revision_ = 0, jobRevision_ = 0;
    bool building_ = false, dirty_ = false;
    int selected_ = 0, field_ = 0;
    QString path_;
    QList<QByteArray> undo_, redo_;
    void buildRibbon();
    void fitRibbon();
    void parkWorkspace();
    void activateWorkspace();
    void showEditor();
    void filterTree();
    void properties(int column);
    void sourceProperties(int column);
    void rebuildEditors();
    void rebuildTree();
    void changeMode(int);
    void selectRow(int);
    void checkpoint();
    void changed(bool rebuild = true);
    void undo(bool redo = false);
    void addView(View);
    void updatePlots();
    void editSurface(int, int);
    void editObject(int, int);
    void editSource(int, int);
    void parameters();
    void calculationParameters();
    void catalog();
    void manualMaterial();
    void importCatalog();
    void addObject(optics::ObjectKind);
    void addSource(optics::SourceShape);
    void removeRow();
    void addSurface();
    void addLens();
    void moveSurface(int);
    void autofocus();
    void optimize();
    void rayReport();
    void paraxialReport();
    void prescription();
    void exportCSV();
    void save(bool as = false);
    void open();
    bool mayDiscard();
    void error(const std::exception&);
};
