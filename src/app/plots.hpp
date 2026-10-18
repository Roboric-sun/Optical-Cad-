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
    QString error, waveError, diffractionError, fanError, geometryWarning;
    int field = 0, selected = -1, detector = 0;
    QSet<int> hidden;
    size_t displayRays = 80;
    int layoutRays = 13;
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
    double zoomX = 1, zoomY = 1, panX = 0, panY = 0;
    struct AxisView {
        QRectF rect;
        double xmin, xmax, ymin, ymax, baseXSpan, baseYSpan;
    };
    const std::vector<AxisView>& axisViews() const { return axes_; }
    void setZoom(double value) { zoom=std::clamp(value,.2,8.); emit viewChanged(zoom); update(); }
    std::function<void(int)> select;
    void setData(std::shared_ptr<Results> data) {
        data_ = std::move(data);
        update();
    }
    void resetView() {
        zoom = 1;
        zoomX = zoomY = 1; panX = panY = 0;
        yaw = 30;
        pitch = 20;
        emit viewChanged(zoom);
        update();
    }
  signals:
    void viewChanged(double zoom);

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
    std::vector<AxisView> axes_;
};
