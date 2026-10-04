#pragma once
#include "project.hpp"
#include <QSet>
#include <QWidget>
#include <memory>

enum class View {
    Layout,
    Spot,
    Fan,
    OPD,
    Wavefront,
    PSF,
    MTF,
    RMS,
    Scene,
    Detector,
    Longitudinal,
    FieldCurvature,
    ChromaticFocus,
    GeometricMTF,
    DetectorProfile
};
struct Results {
    Project project;
    std::vector<optics::Spot> spots;
    optics::Wavefront wave;
    optics::Diffraction diffraction;
    optics::RayFan fan;
    std::array<optics::AnalysisCurve, 4> curves;
    std::optional<optics::SceneTrace> scene;
    std::vector<optics::DetectorStatistics> detectorStats;
    QString error, waveError, fanError;
    int field = 0, selected = -1, detector = 0;
    QSet<int> hidden;
    size_t displayRays = 80;
};
QString viewName(View);
QByteArray rayFanCSV(const optics::RayFan&);
QByteArray detectorProfileCSV(const optics::DetectorStatistics&);
QByteArray detectorStatisticsCSV(const optics::DetectorData&, const optics::DetectorStatistics&,
                                 double launchedPower, bool partial);
class PlotWidget : public QWidget {
    Q_OBJECT
  public:
    explicit PlotWidget(View, QWidget* parent = nullptr);
    View view;
    bool logarithmic = false, grid = true;
    bool officePresentation = false;
    double zoom = 1, yaw = 30, pitch = 20;
    std::function<void(int)> select;
    void setData(std::shared_ptr<Results> data) {
        data_ = std::move(data);
        update();
    }
    void resetView() {
        zoom = 1;
        yaw = 30;
        pitch = 20;
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

  private:
    std::shared_ptr<Results> data_;
    QPoint lastMouse;
    std::vector<QPointF> targets;
};
