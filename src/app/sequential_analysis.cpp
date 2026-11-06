#include "plots.hpp"
#include <atomic>

using namespace optics;

std::shared_ptr<Results> sequentialAnalysis(Project project, int selectedField,
                                            std::shared_ptr<std::atomic<bool>> cancelled) {
    auto result = std::make_shared<Results>();
    result->project = std::move(project);
    result->field = selectedField;
    const Project& model = result->project;
    const int field = result->field;
    auto errors = model.system.validate(model.catalog);
    if (!errors.empty())
        result->error = "Исправьте данные: " + QString::fromStdString(errors.front());
    else
        try {
            result->spots =
                fieldSpots(model.system, model.catalog, [&] { return cancelled->load(); });
            if (cancelled->load())
                return result;
            auto f = model.system.fields.at(field);
            try {
                result->fan = rayFan(model.system, model.catalog, f);
            } catch (const std::exception& e) {
                result->fanError = QString::fromStdString(e.what());
            }
            try {
                result->wave = wavefront(model.system, model.catalog, f);
            } catch (const std::exception& e) {
                result->waveError = QString::fromStdString(e.what());
            }
            try {
                result->diffraction = model.workspace["polychromaticDiffraction"].toBool(false)
                                          ? polychromaticDiffraction(model.system, model.catalog, f)
                                          : diffraction(model.system, model.catalog, f);
            } catch (const std::exception& e) {
                result->diffractionError = QString::fromStdString(e.what());
            }
            try {
                result->curves[0] = longitudinalAberration(model.system, model.catalog);
            } catch (...) {
            }
            try {
                result->curves[1] = fieldCurvature(model.system, model.catalog);
            } catch (...) {
            }
            try {
                result->curves[2] = chromaticFocus(model.system, model.catalog);
            } catch (...) {
            }
            result->curves[3] = geometricMTF(result->spots[field]);
            QString focus;
            try {
                auto p = paraxial(model.system, model.catalog,
                                  model.system.wavelengths[model.system.primary].um);
                focus = QString("EFL %1 мм · f/%2").arg(p.efl, 0, 'f', 3).arg(p.fNumber, 0, 'f', 2);
            } catch (...) {
                try {
                    focus =
                        differentialFirstOrder(model.system, model.catalog,
                                               model.system.wavelengths[model.system.primary].um)
                                .afocal
                            ? "Афокальная система · первый порядок 4×4"
                            : "Локальная система · первый порядок 4×4";
                } catch (...) {
                    focus = "Параксиальный расчёт неприменим";
                }
            }
            if (!std::isfinite(result->spots[field].rms))
                focus += " · На изображении нет мощности";
            const auto clear = clearSemiDiameters(resolvedSystem(model.system, model.catalog));
            int trimmed = 0;
            for (size_t i = 0; i < clear.size(); ++i)
                if (clear[i] < model.system.surfaces[i].semiDiameter - 1e-8)
                    ++trimmed;
            if (trimmed) {
                result->geometryWarning = QString("Края поверхностей обрезаны: %1. Расчёт "
                                                  "продолжается; внешние лучи отсекаются.")
                                              .arg(trimmed);
                focus += " · Обрезка краёв: " + QString::number(trimmed);
            }
            result->summary =
                QString("Режим: последовательный    %1    Полей: %2    Волн: %3   "
                        " RMS: %4 мкм    Готово")
                    .arg(focus)
                    .arg(model.system.fields.size())
                    .arg(model.system.wavelengths.size())
                    .arg(std::isfinite(result->spots[field].rms)
                             ? QString::number(result->spots[field].rms * 1000, 'f', 3)
                             : QStringLiteral("—"));
        } catch (const std::exception& e) {
            result->error = QString::fromStdString(e.what());
        }
    if (!result->error.isEmpty())
        result->summary = result->error;
    return result;
}
