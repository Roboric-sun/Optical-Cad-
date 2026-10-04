#include "plots.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolBar>
#include <QWheelEvent>
#include <algorithm>

using namespace optics;
QByteArray rayFanCSV(const RayFan& fan) {
    QByteArray csv =
        "wavelength_um,pupil,tangential_um,sagittal_um,tangential_valid,sagittal_valid\n";
    auto number = [](double value) {
        return std::isfinite(value) ? QString::number(value, 'g', 14) : QString();
    };
    for (size_t wave = 0; wave < fan.wavelengths.size(); ++wave)
        for (size_t i = 0; i < fan.tangential.x.size(); ++i) {
            double t = fan.tangential.y.at(wave).at(i), s = fan.sagittal.y.at(wave).at(i);
            csv += QString("%1,%2,%3,%4,%5,%6\n")
                       .arg(number(fan.wavelengths[wave]), number(fan.tangential.x[i]), number(t),
                            number(s))
                       .arg(std::isfinite(t) ? 1 : 0)
                       .arg(std::isfinite(s) ? 1 : 0)
                       .toUtf8();
        }
    return csv;
}
static const QColor colors[] = {QColor("#006bc6"), QColor("#268638"), QColor("#de2828"),
                                QColor("#9467bd"), QColor("#bdab2e")};
QString viewName(View v) {
    switch (v) {
    case View::Layout:
        return "Схема системы";
    case View::Spot:
        return "Точечная диаграмма";
    case View::Fan:
        return "Лучевые аберрации";
    case View::OPD:
        return "Разность хода";
    case View::Wavefront:
        return "Волновой фронт";
    case View::PSF:
        return "Функция рассеяния";
    case View::MTF:
        return "ЧКХ (MTF)";
    case View::RMS:
        return "RMS по полям";
    case View::Scene:
        return "3D-модель хода лучей";
    case View::Detector:
        return "Карта облучённости";
    case View::Longitudinal:
        return "Сферохроматизм";
    case View::FieldCurvature:
        return "Кривизна поля";
    case View::ChromaticFocus:
        return "Хроматический фокус";
    case View::GeometricMTF:
        return "Геометрическая MTF";
    }
    return {};
}
static QColor heat(double t) {
    t = std::clamp(t, 0., 1.);
    const QColor c[] = {QColor("#24234a"), QColor("#624682"), QColor("#be536d"), QColor("#f59b47"),
                        QColor("#fff6a2")};
    int i = std::min(3, int(t * 4));
    double f = t * 4 - i;
    return QColor::fromRgbF(c[i].redF() * (1 - f) + c[i + 1].redF() * f,
                            c[i].greenF() * (1 - f) + c[i + 1].greenF() * f,
                            c[i].blueF() * (1 - f) + c[i + 1].blueF() * f);
}
PlotWidget::PlotWidget(View v, QWidget* parent) : QWidget(parent), view(v) {
    setMinimumSize(280, 210);
    if (v == View::Fan)
        setMinimumSize(500, 250);
    setMouseTracking(true);
    setObjectName("plot_" + QString::number(int(v)));
}
void PlotWidget::resizeEvent(QResizeEvent* event) {
    if (auto* bar = findChild<QToolBar*>("plotToolbar", Qt::FindDirectChildrenOnly))
        bar->setGeometry(0, 0, width(), 29);
    QWidget::resizeEvent(event);
}
void PlotWidget::wheelEvent(QWheelEvent* e) {
    zoom = std::clamp(zoom * std::exp(e->angleDelta().y() / 900.0), .2, 8.);
    update();
}
void PlotWidget::mousePressEvent(QMouseEvent* e) {
    if (view == View::Scene && officePresentation) {
        QRectF cube(width() - 94, 50, 68, 68);
        if (cube.contains(e->position())) {
            QPointF local = e->position() - cube.topLeft();
            if (local.y() < 27) {
                pitch = 90;
                yaw = 0;
            } else if (local.x() < 34) {
                pitch = 0;
                yaw = 90;
            } else {
                pitch = 0;
                yaw = 0;
            }
            update();
            return;
        }
    }
    lastMouse = e->pos();
    if (select && e->button() == Qt::LeftButton) {
        double nearest = 28;
        int found = -1;
        for (size_t i = 0; i < targets.size(); ++i) {
            double d = QLineF(targets[i], e->position()).length();
            if (d < nearest) {
                nearest = d;
                found = int(i);
            }
        }
        if (found >= 0)
            select(found);
    }
}
void PlotWidget::mouseMoveEvent(QMouseEvent* e) {
    if (view == View::Scene && (e->buttons() & Qt::LeftButton)) {
        yaw += (e->position().x() - lastMouse.x()) * .5;
        pitch = std::clamp(pitch + (e->position().y() - lastMouse.y()) * .5, -85., 85.);
        lastMouse = e->pos();
        update();
    }
}
void PlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);
    const bool toolbar = findChild<QToolBar*>("plotToolbar", Qt::FindDirectChildrenOnly) != nullptr;
    QRectF area(58, 52, width() - 90, height() - 95);
    if (officePresentation && (view == View::Layout || view == View::Scene))
        area = QRectF(14, toolbar ? 42 : 14, width() - 28, height() - (toolbar ? 56 : 28));
    p.setPen(QColor("#244565"));
    QFont title = font();
    title.setPointSize(13);
    title.setBold(true);
    p.setFont(title);
    if (!officePresentation)
        p.drawText(QRectF(20, 12, width() - 40, 26), viewName(view));
    p.setFont(font());
    if (!data_) {
        p.drawText(area, Qt::AlignCenter, "Нет данных");
        return;
    }
    auto& d = *data_;
    auto& sys = d.project.system;
    auto& cat = d.project.catalog;
    if (!d.error.isEmpty()) {
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, d.error);
        return;
    }
    if (view == View::Detector && officePresentation) {
        const double top = toolbar ? 46 : 20;
        const double side = std::max(80., std::min(height() - top - 12., width() * .28));
        const QRectF mapRect(12, top, side, side);
        const DetectorData* detector = nullptr;
        const SceneTrace* scene = d.scene ? &*d.scene : nullptr;
        if (scene && !scene->detectors.empty())
            detector = &scene->detectors[std::min(size_t(d.detector), scene->detectors.size() - 1)];
        double maximum = 0, mean = 0;
        p.fillRect(mapRect, QColor("#292348"));
        if (detector) {
            const auto& dt = *detector;
            maximum = *std::max_element(dt.watts.begin(), dt.watts.end()) / dt.cellArea;
            mean = dt.totalPower() / (dt.cellArea * dt.nx * dt.ny);
            QImage image(dt.nx, dt.ny, QImage::Format_RGB32);
            for (int y = 0; y < dt.ny; ++y)
                for (int x = 0; x < dt.nx; ++x) {
                    double t = maximum > 0 ? dt.watts[y * dt.nx + x] / dt.cellArea / maximum : 0;
                    if (logarithmic)
                        t = std::log10(1 + 999 * t) / 3;
                    image.setPixelColor(x, dt.ny - 1 - y, heat(t));
                }
            p.drawImage(mapRect, image);
        } else {
            p.setPen(QColor("#ddd9ea"));
            p.drawText(mapRect.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap,
                       "Запустите трассировку\nдля расчёта карты");
        }
        p.setPen(QColor("#b4c4d3"));
        p.drawRect(mapRect);
        const double barX = mapRect.right() + 16;
        for (int i = 0; i < 100; ++i)
            p.fillRect(QRectF(barX, top + i * side / 100, 13, side / 100 + 1), heat(1 - i / 100.));
        QFont small = font();
        small.setPixelSize(10);
        p.setFont(small);
        p.setPen(QColor("#7a8b9a"));
        p.drawText(QRectF(barX - 4, top - 17, 72, 12), "Вт/мм²");
        for (int i = 0; i < 5; ++i) {
            double t = 1 - i / 4.;
            double value = logarithmic ? maximum * (std::pow(1000., t) - 1) / 999 : maximum * t;
            p.drawText(QRectF(barX + 18, top + i * side / 4. - 5, 62, 14),
                       QString::number(value, 'g', 3));
        }
        const double left = barX + 91;
        QFont bold = font();
        bold.setPixelSize(12);
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(QColor("#245f99"));
        p.drawText(QRectF(left, top + 3, width() - left - 12, 20), "Результаты расчёта");
        auto percentage = [&](size_t n) {
            return QString("%1 (%2 %)")
                .arg(n)
                .arg(scene && scene->launched ? 100. * n / scene->launched : 0, 0, 'f', 1);
        };
        auto power = [](double v) { return QString::number(v, 'g', 5) + " Вт"; };
        const QStringList labels = {
            "Трассировано лучей",    "Достигло детектора",    "Поглощено телами",
            "Потеряно вне сцены",    "Максимум облучённости", "Средняя облучённость",
            "Мощность на детекторе", "Предел сегментов",      "Время расчёта"};
        const QStringList values = {scene ? QString::number(scene->launched) : "—",
                                    detector ? percentage(detector->hits) : "—",
                                    scene ? percentage(scene->absorbed) : "—",
                                    scene ? percentage(scene->escaped) : "—",
                                    detector ? QString::number(maximum, 'g', 5) + " Вт/мм²" : "—",
                                    detector ? QString::number(mean, 'g', 5) + " Вт/мм²" : "—",
                                    detector ? power(detector->totalPower()) : "—",
                                    scene ? power(scene->truncatedPower) : "—",
                                    scene ? QString::number(scene->seconds, 'f', 3) + " с" : "—"};
        const double gap = std::min(20., (side - 30) / 9);
        const double valueX = std::min(left + 235., width() - 168.);
        for (int i = 0; i < labels.size(); ++i) {
            const double y = top + 29 + i * gap;
            p.setFont(small);
            p.setPen(QColor("#75889c"));
            p.drawText(QRectF(left, y, std::max(80., valueX - left - 10), gap), Qt::AlignVCenter,
                       labels[i]);
            p.setFont(bold);
            p.setPen(QColor("#20364d"));
            p.drawText(QRectF(valueX, y, width() - valueX - 14, gap), Qt::AlignVCenter, values[i]);
        }
        return;
    }
    auto axes = [&](double xmin, double xmax, double ymin, double ymax, QString xl, QString yl) {
        if (xmax <= xmin)
            xmax = xmin + 1;
        if (ymax <= ymin)
            ymax = ymin + 1;
        p.setPen(QPen(QColor("#e5edf4"), 1));
        for (int i = 0; i <= 4; ++i) {
            double f = i / 4.;
            if (grid) {
                p.drawLine(QPointF(area.left() + area.width() * f, area.top()),
                           QPointF(area.left() + area.width() * f, area.bottom()));
                p.drawLine(QPointF(area.left(), area.top() + area.height() * f),
                           QPointF(area.right(), area.top() + area.height() * f));
            }
            p.setPen(QColor("#637b8e"));
            if (!(officePresentation && view == View::Layout)) {
                p.drawText(QRectF(area.left() + area.width() * f - 40, area.bottom() + 6, 80, 18),
                           Qt::AlignCenter, QString::number(xmin + (xmax - xmin) * f, 'g', 4));
                p.drawText(QRectF(0, area.bottom() - area.height() * f - 9, 50, 18), Qt::AlignRight,
                           QString::number(ymin + (ymax - ymin) * f, 'g', 4));
            }
            p.setPen(QPen(QColor("#e5edf4"), 1));
        }
        p.setPen(QColor("#d0deec"));
        p.drawRect(area);
        p.setPen(QColor("#637b8e"));
        if (!(officePresentation && view == View::Layout)) {
            p.drawText(QRectF(area.left(), height() - 24, area.width(), 18), Qt::AlignCenter, xl);
            p.drawText(QRectF(20, 35, width() - 40, 18), Qt::AlignLeft, yl);
        }
        return [=](double x, double y) {
            return QPointF(area.left() + (x - xmin) / (xmax - xmin) * area.width(),
                           area.bottom() - (y - ymin) / (ymax - ymin) * area.height());
        };
    };
    targets.clear();
    if (view == View::Layout) {
        auto verts = sys.vertices();
        double length = std::max(sys.imageZ(), verts.back() + 5),
               height = std::max(15., sys.pupilDiameter * 1.2);
        for (auto& s : sys.surfaces)
            height = std::max(height, s.semiDiameter * 1.3);
        for (auto field : sys.fields)
            height = std::max(height, std::abs(tan(field.y * deg) * length) * 1.15 + 10);
        auto map = axes(-25, length + 5, -height / zoom, height / zoom, "Z, мм", "Y, мм");
        p.save();
        p.setClipRect(area);
        p.setPen(QPen(QColor("#bdcbd6"), 1, Qt::DashLine));
        p.drawLine(map(-25, 0), map(length + 5, 0));
        for (size_t i = 0; i < sys.surfaces.size(); ++i) {
            auto& s = sys.surfaces[i];
            Pose pose{{s.decenter.x, s.decenter.y, verts[i] + s.decenter.z}, s.tilt};
            QPainterPath path;
            for (int k = 0; k <= 100; ++k) {
                double y = (2. * k / 100 - 1) * s.semiDiameter;
                Vec3 pos = pose.world({0, y, sag(s, 0, y)});
                QPointF point = map(pos.z, pos.y);
                if (k == 0)
                    path.moveTo(point);
                else
                    path.lineTo(point);
            }
            targets.push_back(map(verts[i], 0));
            if (d.hidden.contains(int(i)))
                continue;
            p.setPen(QPen(int(i) == d.selected ? QColor("#ea9c36") : QColor("#3c7aaa"),
                          int(i) == d.selected ? 3 : 2));
            p.drawPath(path);
            if (s.material != "AIR" && i + 1 < sys.surfaces.size()) {
                auto& back = sys.surfaces[i + 1];
                for (int k = 100; k >= 0; --k) {
                    double y = (2. * k / 100 - 1) * std::min(s.semiDiameter, back.semiDiameter);
                    path.lineTo(map(verts[i + 1] + sag(back, 0, y), y));
                }
                path.closeSubpath();
                p.fillPath(path, QColor(100, 163, 212, 55));
            }
            if (i == sys.stop) {
                p.setPen(QPen(QColor("#d56055"), 3));
                p.drawLine(map(verts[i], s.semiDiameter), map(verts[i], s.semiDiameter + 4));
                p.drawLine(map(verts[i], -s.semiDiameter), map(verts[i], -s.semiDiameter - 4));
            }
        }
        for (size_t fi = 0; fi < sys.fields.size(); ++fi) {
            p.setPen(QPen(colors[fi % 5], 1.1));
            for (int j = -6; j <= 6; ++j) {
                auto t = trace(
                    sys, cat,
                    pupilRay(sys, cat, sys.fields[fi], sys.wavelengths[sys.primary].um, 0, j / 6.));
                QPolygonF line;
                for (auto point : t.points)
                    line << map(point.z, point.y);
                p.drawPolyline(line);
            }
        }
        p.setPen(QPen(QColor("#68a448"), 2));
        p.drawLine(map(sys.imageZ(), -height), map(sys.imageZ(), height));
        p.restore();
        if (officePresentation) {
            QRectF legend(area.left() + 14, area.top() + 14, 150, 32 + sys.fields.size() * 14);
            p.setPen(QColor("#cbdbea"));
            p.setBrush(Qt::white);
            p.drawRoundedRect(legend, 2, 2);
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor("#20364d"));
            p.drawText(legend.adjusted(10, 6, -5, -5), "Поля зрения");
            f.setPixelSize(10);
            f.setBold(false);
            p.setFont(f);
            for (size_t i = 0; i < sys.fields.size(); ++i) {
                double y = legend.top() + 32 + i * 14;
                p.setPen(QPen(colors[i % 5], 1.5));
                p.drawLine(QPointF(legend.left() + 10, y), QPointF(legend.left() + 30, y));
                p.setPen(QColor("#7a8b9a"));
                p.drawText(QRectF(legend.left() + 38, y - 7, 100, 14),
                           QString::number(sys.fields[i].y, 'g', 3) + "°");
            }
            p.setPen(QColor("#d75351"));
            p.drawText(map(verts[sys.stop], sys.surfaces[sys.stop].semiDiameter + 5) +
                           QPointF(-16, -4),
                       "STOP");
            p.setPen(QColor("#68a448"));
            const auto label = map(sys.imageZ(), height * .77) + QPointF(-72, -5);
            p.drawText(QRectF(label, QSizeF(95, 34)), Qt::AlignLeft, "Плоскость\nизображения");
        }
    } else if (view == View::Spot) {
        if (d.spots.empty())
            return;
        const auto& sp = d.spots.at(std::min(size_t(d.field), d.spots.size() - 1));
        double limit = std::max(.001, sp.maxRadius * 1.15) * 1000 / zoom;
        auto map = axes(-limit, limit, -limit, limit, "X, мкм", "Y, мкм · полихроматическое пятно");
        p.save();
        p.setClipRect(area);
        for (const auto& sample : sp.samples) {
            size_t wave = 0;
            for (size_t i = 0; i < sys.wavelengths.size(); ++i)
                if (std::abs(sys.wavelengths[i].um - sample.wavelength) < 1e-9)
                    wave = i;
            p.setPen(colors[wave % 5]);
            p.drawPoint(map((sample.image.x - sp.centroid.x) * 1000,
                            (sample.image.y - sp.centroid.y) * 1000));
        }
        p.restore();
        p.setPen(QColor("#637b8e"));
        p.drawText(QRectF(area.left() + 10, area.top() + 8, 350, 20),
                   QString("RMS %1 мкм · %2 / %3 лучей")
                       .arg(sp.rms * 1000, 0, 'f', 3)
                       .arg(sp.samples.size())
                       .arg(sp.launched));
    } else if (view == View::Fan) {
        if (!d.fanError.isEmpty()) {
            p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, d.fanError);
            return;
        }
        const auto& fan = d.fan;
        if (fan.wavelengths.empty())
            return;
        double limit = 1;
        for (const auto* curve : {&fan.tangential, &fan.sagittal})
            for (const auto& series : curve->y)
                for (double value : series)
                    if (std::isfinite(value))
                        limit = std::max(limit, std::abs(value) * 1.1);
        limit /= zoom;
        QFont small = font();
        small.setPixelSize(11);
        p.setFont(small);
        for (size_t wave = 0; wave < fan.wavelengths.size(); ++wave) {
            double x = 58 + wave * 110;
            p.setPen(QPen(colors[wave % 5], 1.6));
            p.drawLine(QPointF(x, 49), QPointF(x + 20, 49));
            p.setPen(QColor("#637b8e"));
            p.drawText(QRectF(x + 25, 41, 82, 18),
                       QString::number(fan.wavelengths[wave] * 1000, 'f', 1) + " нм");
        }
        const double gap = 88;
        const double panelWidth = (area.width() - gap) / 2;
        for (int plane = 0; plane < 2; ++plane) {
            QRectF panel(area.left() + plane * (panelWidth + gap), 83, panelWidth, height() - 151);
            const auto& curve = plane ? fan.sagittal : fan.tangential;
            auto map = [&](double x, double y) {
                return QPointF(panel.left() + (x + 1) * panel.width() / 2,
                               panel.center().y() - y / limit * panel.height() / 2);
            };
            p.setPen(QColor("#245c8d"));
            p.drawText(QRectF(panel.left(), 61, panel.width(), 18), Qt::AlignCenter,
                       plane ? "Сагиттальная аберрация, мкм" : "Тангенциальная аберрация, мкм");
            for (int tick = 0; tick <= 4; ++tick) {
                double x = -1 + tick * .5, y = -limit + tick * limit / 2;
                if (grid) {
                    p.setPen(QPen(QColor("#e5edf4"), 1));
                    p.drawLine(map(x, -limit), map(x, limit));
                    p.drawLine(map(-1, y), map(1, y));
                }
                p.setPen(QColor("#637b8e"));
                p.drawText(QRectF(map(x, -limit).x() - 26, panel.bottom() + 5, 52, 18),
                           Qt::AlignCenter, QString::number(x));
                p.drawText(QRectF(panel.left() - 54, map(-1, y).y() - 9, 48, 18), Qt::AlignRight,
                           QString::number(y, 'g', 4));
            }
            p.setPen(QColor("#d0deec"));
            p.drawRect(panel);
            p.setPen(QColor("#637b8e"));
            p.drawText(QRectF(panel.left(), panel.bottom() + 27, panel.width(), 18),
                       Qt::AlignCenter, plane ? "Координата зрачка S" : "Координата зрачка T");
            p.save();
            p.setClipRect(panel);
            for (size_t wave = 0; wave < curve.y.size(); ++wave) {
                QPainterPath line;
                bool connected = false;
                for (size_t i = 0; i < curve.x.size(); ++i) {
                    double value = curve.y[wave][i];
                    if (!std::isfinite(value)) {
                        connected = false;
                        continue;
                    }
                    if (connected)
                        line.lineTo(map(curve.x[i], value));
                    else
                        line.moveTo(map(curve.x[i], value));
                    connected = true;
                }
                p.setPen(QPen(colors[wave % 5], 1.5));
                p.drawPath(line);
            }
            p.restore();
        }
        p.setPen(QColor("#637b8e"));
        p.drawText(QRectF(20, height() - 21, width() - 40, 18), Qt::AlignCenter,
                   "Отсчёт от главного луча первичной волны; пропуски — отсечённые лучи");
    } else if (view == View::OPD || view == View::Wavefront) {
        if (!d.waveError.isEmpty()) {
            p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, d.waveError);
            return;
        }
        const auto& wave = d.wave;
        if (wave.samples.empty())
            return;
        double limit = .1;
        for (auto sample : wave.samples)
            limit = std::max(limit, std::abs(sample.opd * 1000 / wave.wavelength));
        if (view == View::OPD) {
            auto map = axes(-1, 1, -limit * 1.1, limit * 1.1, "Координата зрачка Y",
                            "OPD, волн · первичная длина волны");
            QPolygonF samples;
            for (auto sample : wave.samples)
                if (std::abs(sample.px) < 1e-6)
                    samples << QPointF(sample.py, sample.opd * 1000 / wave.wavelength);
            std::sort(samples.begin(), samples.end(),
                      [](QPointF a, QPointF b) { return a.x() < b.x(); });
            QPolygonF poly;
            for (auto point : samples)
                poly << map(point.x(), point.y());
            p.setPen(QPen(colors[0], 1.6));
            p.drawPolyline(poly);
        } else {
            const double side = std::min(area.height(), area.width() * .65);
            area.setSize({side, side});
            auto map =
                axes(-1, 1, -1, 1, "Нормированный зрачок X", "OPD, волн · первичная длина волны");
            double spacing = 2.;
            for (auto sample : wave.samples)
                if (sample.px > 1e-6)
                    spacing = std::min(spacing, sample.px);
            p.save();
            p.setClipRect(area);
            p.setPen(Qt::NoPen);
            for (auto sample : wave.samples) {
                double opd = sample.opd * 1000 / wave.wavelength;
                auto point = map(sample.px, sample.py);
                double cell = side * spacing / 2.;
                p.fillRect(QRectF(point.x() - cell / 2, point.y() - cell / 2, cell + .4, cell + .4),
                           heat(.5 + opd / (2 * limit)));
            }
            p.restore();
            for (int i = 0; i < 100; ++i)
                p.fillRect(
                    QRectF(area.right() + 16, area.top() + i * side / 100, 13, side / 100 + 1),
                    heat(1 - i / 100.));
            p.setPen(QColor("#637b8e"));
            p.drawText(QPointF(area.right() + 34, area.top() + 8), QString::number(limit, 'g', 4));
            p.drawText(QPointF(area.right() + 34, area.bottom()), QString::number(-limit, 'g', 4));
        }
        p.setPen(QColor("#637b8e"));
        p.drawText(QRectF(view == View::Wavefront ? area.right() + 70 : area.right() - 230,
                          view == View::Wavefront ? area.top() + 42 : 35, 220, 42),
                   QString("RMS %1 λ\nPV %2 λ")
                       .arg(wave.rms * 1000 / wave.wavelength, 0, 'g', 4)
                       .arg(wave.pv * 1000 / wave.wavelength, 0, 'g', 4));
    } else if (view == View::MTF) {
        if (!d.waveError.isEmpty()) {
            p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, d.waveError);
            return;
        }
        auto& df = d.diffraction;
        if (df.frequency.empty())
            return;
        auto map = axes(0, df.frequency.back(), 0, 1, "Пространственная частота, пар линий/мм",
                        "Скалярная монохроматическая MTF · X и Y");
        for (int a = 0; a < 2; ++a) {
            QPolygonF line;
            auto& mtf = a ? df.mtfY : df.mtfX;
            for (size_t i = 0; i < mtf.size(); ++i)
                line << map(df.frequency[i], mtf[i]);
            p.setPen(QPen(colors[a], 1.6));
            p.drawPolyline(line);
        }
    } else if (view == View::PSF || view == View::Detector) {
        std::vector<double> values;
        int nx = 0, ny = 0;
        double physicalX = 0, physicalY = 0;
        QString unit, summary;
        if (view == View::PSF) {
            if (!d.waveError.isEmpty()) {
                p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, d.waveError);
                return;
            }
            auto& df = d.diffraction;
            nx = ny = df.size;
            values = df.psf;
            physicalX = physicalY = df.pixelUm * nx;
            unit = "мкм";
            summary = "Монохроматическая PSF\nНормированная энергия";
        } else {
            if (!d.scene || d.scene->detectors.empty()) {
                p.drawText(area, Qt::AlignCenter, "Запустите трассировку сцены");
                return;
            }
            auto& dt =
                d.scene->detectors.at(std::min(size_t(d.detector), d.scene->detectors.size() - 1));
            auto& o = d.project.scene.objects[dt.objectIndex];
            nx = dt.nx;
            ny = dt.ny;
            physicalX = o.size.x;
            physicalY = o.size.y;
            unit = "мм";
            values = dt.watts;
            for (double& v : values)
                v /= dt.cellArea;
            summary = QString("%1\nМощность %2 Вт\nПопало %3 лучей")
                          .arg(QString::fromStdString(o.name))
                          .arg(dt.totalPower(), 0, 'g', 5)
                          .arg(dt.hits);
        }
        if (values.empty())
            return;
        double maximum = *std::max_element(values.begin(), values.end());
        double side = std::min(area.height(), area.width() * .65);
        area.setSize({side, side * physicalY / physicalX});
        if (area.height() > height() - 100)
            area.setHeight(height() - 100);
        axes(-physicalX / 2, physicalX / 2, -physicalY / 2, physicalY / 2, "X, " + unit,
             "Y, " + unit + (logarithmic ? " · логарифмическая шкала" : " · линейная шкала"));
        QImage image(nx, ny, QImage::Format_RGB32);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                double t = maximum > 0 ? values[y * nx + x] / maximum : 0;
                if (logarithmic)
                    t = std::log10(1 + 999 * t) / 3;
                image.setPixelColor(x, ny - 1 - y, heat(t));
            }
        p.drawImage(area, image);
        for (int i = 0; i < 100; ++i)
            p.fillRect(QRectF(area.right() + 15, area.top() + i * area.height() / 100, 12,
                              area.height() / 100 + 1),
                       heat(1 - i / 100.));
        p.setPen(QColor("#637b8e"));
        p.drawText(QRectF(area.right() + 40, area.top(), width() - area.right() - 50,
                          height() - area.top() - 20),
                   Qt::TextWordWrap,
                   QString("max %1 %2\n\n%3")
                       .arg(maximum, 0, 'g', 4)
                       .arg(view == View::Detector ? "Вт/мм²" : "")
                       .arg(summary));
    } else if (view == View::RMS) {
        double limit = 1;
        for (auto& sp : d.spots)
            limit = std::max(limit, sp.rms * 1000 * 1.2);
        auto map = axes(0, std::max(1., double(d.spots.size() - 1)), 0, limit, "Номер поля зрения",
                        "Полихроматический RMS, мкм");
        QPolygonF line;
        for (size_t i = 0; i < d.spots.size(); ++i)
            line << map(i, d.spots[i].rms * 1000);
        p.setPen(QPen(colors[0], 1.6));
        p.drawPolyline(line);
        p.setBrush(colors[0]);
        for (auto a : line)
            p.drawEllipse(a, 3, 3);
    } else if (int(view) >= 10) {
        auto& curve = d.curves[int(view) - 10];
        if (curve.x.empty()) {
            p.drawText(area, Qt::AlignCenter, "Анализ неприменим к этой системе");
            return;
        }
        double xmin = *std::min_element(curve.x.begin(), curve.x.end()),
               xmax = *std::max_element(curve.x.begin(), curve.x.end()), ymin = 1e100,
               ymax = -1e100;
        for (auto& row : curve.y)
            for (double v : row)
                if (std::isfinite(v)) {
                    ymin = std::min(ymin, v);
                    ymax = std::max(ymax, v);
                }
        if (ymin == 1e100) {
            p.drawText(area, Qt::AlignCenter, "Недостаточно прошедших лучей");
            return;
        }
        double padding = std::max(1e-3, (ymax - ymin) * .1);
        QString xl, yl;
        if (view == View::Longitudinal) {
            xl = "Нормированная высота зрачка";
            yl = "Продольная аберрация, мм · цвет по длине волны";
        } else if (view == View::FieldCurvature) {
            xl = "Угол поля, °";
            yl = "Положение фокуса относительно изображения, мм · T / S";
        } else if (view == View::ChromaticFocus) {
            xl = "Длина волны, нм";
            yl = "Сдвиг параксиального фокуса от первичной волны, мм";
        } else {
            xl = "Частота, пар линий/мм";
            yl = "Геометрическая полихроматическая MTF · X / Y";
            ymin = 0;
            ymax = 1;
            padding = 0;
        }
        auto map = axes(xmin, xmax, ymin - padding, ymax + padding, xl, yl);
        for (size_t j = 0; j < curve.y.size(); ++j) {
            p.setPen(QPen(colors[j % 5], 1.6));
            QPolygonF line;
            for (size_t i = 0; i < curve.x.size(); ++i) {
                if (std::isfinite(curve.y[j][i]))
                    line << map(curve.x[i], curve.y[j][i]);
                else {
                    p.drawPolyline(line);
                    line.clear();
                }
            }
            p.drawPolyline(line);
        }
    } else if (view == View::Scene) {
        const auto& scene = d.project.scene;
        Vec3 center;
        double count = 0;
        for (const auto& o : scene.objects) {
            center = center + o.pose.position;
            ++count;
        }
        for (const auto& s : scene.sources) {
            center = center + s.pose.position;
            ++count;
        }
        center = center / std::max(1., count);
        double extent = 30;
        for (const auto& o : scene.objects)
            extent = std::max(extent, (o.pose.position - center).norm() + o.size.norm() / 2);
        for (const auto& s : scene.sources)
            extent = std::max(extent, (s.pose.position - center).norm());
        double scale = std::min(area.width(), area.height()) / (2.3 * extent) * zoom;
        auto project = [&](Vec3 v) {
            v = v - center;
            double a = yaw * deg, b = pitch * deg;
            double x = cos(a) * v.x - sin(a) * v.y, y = sin(a) * v.x + cos(a) * v.y;
            return QPointF(area.center().x() + x * scale,
                           area.center().y() - (cos(b) * v.z - sin(b) * y) * scale);
        };
        p.save();
        p.setClipRect(area);
        p.setPen(QPen(QColor("#e3edf5"), 1));
        if (grid)
            for (int i = -5; i <= 5; ++i) {
                p.drawLine(project({i * 10., -50, 0}), project({i * 10., 50, 0}));
                p.drawLine(project({-50, i * 10., 0}), project({50, i * 10., 0}));
            }
        for (size_t i = 0; i < scene.objects.size(); ++i) {
            const auto& o = scene.objects[i];
            targets.push_back(project(o.pose.position));
            if (d.hidden.contains(int(i)))
                continue;
            QColor color = int(i) == d.selected             ? QColor("#e49a3c")
                           : o.kind == ObjectKind::Detector ? QColor("#68a14f")
                                                            : QColor("#568ab1");
            p.setPen(QPen(color, 1.3));
            auto ring = [&](double z, double radius) {
                QPolygonF poly;
                for (int j = 0; j <= 60; ++j) {
                    double a = 2 * pi * j / 60;
                    poly << project(o.pose.world({radius * cos(a), radius * sin(a), z}));
                }
                p.drawPolyline(poly);
            };
            if (o.kind == ObjectKind::Sphere) {
                for (int axis = 0; axis < 3; ++axis) {
                    QPolygonF poly;
                    for (int j = 0; j <= 60; ++j) {
                        double a = 2 * pi * j / 60, r = o.size.x / 2;
                        Vec3 point = axis == 0   ? Vec3{r * cos(a), r * sin(a), 0}
                                     : axis == 1 ? Vec3{r * cos(a), 0, r * sin(a)}
                                                 : Vec3{0, r * cos(a), r * sin(a)};
                        poly << project(o.pose.world(point));
                    }
                    p.drawPolyline(poly);
                }
            } else if (o.kind == ObjectKind::Lens) {
                Surface front, back;
                front.radius = o.radius1;
                back.radius = o.radius2;
                double r = o.size.x / 2;
                ring(-o.size.z / 2 + sag(front, r, 0), r);
                ring(o.size.z / 2 + sag(back, r, 0), r);
                for (int meridian = 0; meridian < 2; ++meridian) {
                    QPolygonF silhouette;
                    for (int side = 0; side < 2; ++side)
                        for (int j = 0; j <= 60; ++j) {
                            double h = (2. * (side ? 60 - j : j) / 60 - 1) * r,
                                   x = meridian ? 0 : h, y = meridian ? h : 0;
                            silhouette
                                << project(o.pose.world({x, y,
                                                         (side ? o.size.z / 2 : -o.size.z / 2) +
                                                             sag(side ? back : front, x, y)}));
                        }
                    p.setBrush(QColor(114, 175, 214, 55));
                    p.drawPolygon(silhouette);
                    p.setBrush(Qt::NoBrush);
                }
                for (int meridian = 0; meridian < 2; ++meridian)
                    for (int side = 0; side < 2; ++side) {
                        QPolygonF poly;
                        for (int j = 0; j <= 60; ++j) {
                            double h = (2. * j / 60 - 1) * r, x = meridian ? 0 : h,
                                   y = meridian ? h : 0;
                            poly << project(o.pose.world({x, y,
                                                          (side ? o.size.z / 2 : -o.size.z / 2) +
                                                              sag(side ? back : front, x, y)}));
                        }
                        p.drawPolyline(poly);
                    }
            } else if (o.kind == ObjectKind::Prism) {
                auto vertices = prismVertices(o.size);
                for (auto face : prismFaces) {
                    QPolygonF polygon;
                    for (int vertex : face)
                        polygon << project(o.pose.world(vertices[vertex]));
                    p.setBrush(QColor(color.red(), color.green(), color.blue(), 28));
                    p.drawPolygon(polygon);
                }
                p.setBrush(Qt::NoBrush);
            } else if (o.kind == ObjectKind::Cylinder) {
                ring(-o.size.z / 2, o.size.x / 2);
                ring(o.size.z / 2, o.size.x / 2);
                for (int j = 0; j < 4; ++j) {
                    double a = j * pi / 2;
                    p.drawLine(project(o.pose.world(
                                   {o.size.x / 2 * cos(a), o.size.x / 2 * sin(a), -o.size.z / 2})),
                               project(o.pose.world(
                                   {o.size.x / 2 * cos(a), o.size.x / 2 * sin(a), o.size.z / 2})));
                }
            } else {
                std::vector<Vec3> corners;
                for (double z : {-o.size.z / 2, o.size.z / 2})
                    for (Vec3 xy :
                         {Vec3{-o.size.x / 2, -o.size.y / 2, 0},
                          Vec3{o.size.x / 2, -o.size.y / 2, 0}, Vec3{o.size.x / 2, o.size.y / 2, 0},
                          Vec3{-o.size.x / 2, o.size.y / 2, 0}})
                        corners.push_back(o.pose.world({xy.x, xy.y, z}));
                QPolygonF poly;
                for (int k = 0; k < 4; ++k)
                    poly << project(corners[k]);
                p.setBrush(QColor(color.red(), color.green(), color.blue(),
                                  o.interaction == Interaction::Absorb ? 25 : 40));
                p.drawPolygon(poly);
                p.setBrush(Qt::NoBrush);
                for (int k = 0; k < 4; ++k) {
                    p.drawLine(project(corners[k]), project(corners[(k + 1) % 4]));
                    p.drawLine(project(corners[k + 4]), project(corners[(k + 1) % 4 + 4]));
                    p.drawLine(project(corners[k]), project(corners[k + 4]));
                }
            }
            QFont label = font();
            label.setPixelSize(11);
            p.setFont(label);
            p.setPen(o.kind == ObjectKind::Detector ? QColor("#68a14f") : QColor("#7e91a2"));
            p.drawText(project(o.pose.position) + QPointF(18, -8), QString::fromStdString(o.name));
        }
        if (d.scene) {
            const QColor rayColors[] = {QColor("#2478ce"), QColor("#d99538"), QColor("#bc635a")};
            for (size_t j = 0; j < std::min(d.displayRays, d.scene->paths.size()); ++j) {
                const auto& path = d.scene->paths[j];
                for (size_t k = 1; k < path.size(); ++k) {
                    unsigned char kind =
                        j < d.scene->pathKinds.size() && k - 1 < d.scene->pathKinds[j].size()
                            ? d.scene->pathKinds[j][k - 1]
                            : 0;
                    p.setPen(QPen(rayColors[std::min(2, int(kind))], .8));
                    p.drawLine(project(path[k - 1]), project(path[k]));
                }
            }
        }
        p.setPen(QPen(QColor("#edab32"), 2));
        p.setBrush(QColor("#ffc966"));
        QSet<int> labelled;
        for (size_t index = 0; index < scene.sources.size(); ++index) {
            if (labelled.contains(int(index)))
                continue;
            const auto& source = scene.sources[index];
            QStringList wavelengths;
            int count = 0;
            for (size_t other = index; other < scene.sources.size(); ++other)
                if ((source.pose.position - scene.sources[other].pose.position).norm2() < 1e-10) {
                    labelled.insert(int(other));
                    ++count;
                    auto wavelength =
                        QString::number(scene.sources[other].wavelength * 1000, 'f', 1);
                    if (!wavelengths.contains(wavelength))
                        wavelengths << wavelength;
                }
            auto point = project(source.pose.position);
            p.drawEllipse(point, 5, 5);
            if (count == 1)
                p.drawText(point + QPointF(-25, 19), QString::fromStdString(source.name));
            else
                p.drawText(
                    QRectF(point.x() - 300, point.y() + 15, 285, 32),
                    Qt::AlignRight | Qt::TextWordWrap,
                    QString("Источников: %1 · %2 нм").arg(count).arg(wavelengths.join(" / ")));
        }
        p.restore();
        if (officePresentation) {
            p.setPen(QColor("#d0deec"));
            p.drawRect(area);
            QRectF legend(area.left() + 14, area.top() + 14, 190, 75);
            p.setBrush(Qt::white);
            p.drawRoundedRect(legend, 2, 2);
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(QColor("#20364d"));
            p.drawText(legend.adjusted(10, 6, -4, -4), "Отображение лучей");
            const QStringList labels = {"прямые от источника", "после отражения",
                                        "после рассеяния"};
            const QColor c[] = {QColor("#2478ce"), QColor("#d99538"), QColor("#bc635a")};
            f.setPixelSize(10);
            f.setBold(false);
            p.setFont(f);
            for (int i = 0; i < 3; ++i) {
                double y = legend.top() + 31 + i * 13;
                p.setPen(QPen(c[i], 1.5));
                p.drawLine(QPointF(legend.left() + 10, y), QPointF(legend.left() + 30, y));
                p.setPen(QColor("#7a8b9a"));
                p.drawText(QRectF(legend.left() + 37, y - 7, 145, 14), labels[i]);
            }
            f.setPixelSize(8);
            p.setFont(f);
            QPointF o(width() - 94, 50);
            QPolygonF top, left, right;
            top << o + QPointF(34, 0) << o + QPointF(66, 18) << o + QPointF(34, 36)
                << o + QPointF(2, 18);
            left << o + QPointF(2, 18) << o + QPointF(34, 36) << o + QPointF(34, 68)
                 << o + QPointF(2, 50);
            right << o + QPointF(34, 36) << o + QPointF(66, 18) << o + QPointF(66, 50)
                  << o + QPointF(34, 68);
            p.setPen(QColor("#98acbd"));
            p.setBrush(QColor("#f3f7fb"));
            p.drawPolygon(top);
            p.setBrush(QColor("#e8eff5"));
            p.drawPolygon(left);
            p.setBrush(QColor("#f7fafd"));
            p.drawPolygon(right);
            p.setPen(QColor("#7e91a2"));
            p.drawText(QRectF(o.x() + 7, o.y() + 12, 54, 17), Qt::AlignCenter, "Сверху");
            p.drawText(QRectF(o.x() + 3, o.y() + 37, 31, 15), Qt::AlignCenter, "Слева");
            p.drawText(QRectF(o.x() + 33, o.y() + 37, 34, 15), Qt::AlignCenter, "Спереди");
        }
    }
}
