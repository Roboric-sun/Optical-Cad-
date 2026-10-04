// Учебный пример: docs/LEARNING_CPP_RU.md. Использует то же ядро, что и окно Qt.
#include "optics/model.hpp"
#include <iomanip>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        double radius = 50; // мм; без аргумента используется стандартная линза
        if (argc > 2) throw std::invalid_argument("Usage: optics_learning [radius_mm]");
        if (argc == 2) {
            const std::string text = argv[1];
            size_t consumed = 0;
            radius = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(radius) || radius < 25 || radius > 2000)
                throw std::invalid_argument("Radius must be a number from 25 to 2000 mm");
        }

        optics::Catalog catalog;                    // каталог материалов
        auto system = optics::SequentialSystem::demo(); // две поверхности линзы
        system.surfaces[0].radius = radius;          // первая: индекс 0
        system.surfaces[1].radius = -radius;         // вторая: индекс 1
        system.surfaces[0].semiDiameter = system.surfaces[1].semiDiameter = 10;
        auto requireValid = [&] {
            const auto errors = system.validate(catalog);
            if (!errors.empty()) throw std::invalid_argument(errors.front());
        };
        requireValid();
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "1. Lens: R1=" << radius << " mm, R2=" << -radius
                  << " mm, thickness=" << system.surfaces[0].thickness << " mm\n";
        const double wavelength = system.wavelengths[system.primary].um;
        std::cout << "   N-BK7 n=" << catalog.get("N-BK7").index(wavelength)
                  << " at " << wavelength << " um\n";

        const auto paraxial = optics::paraxial(system, catalog, wavelength);
        std::cout << "2. EFL=" << paraxial.efl << " mm, BFL=" << paraxial.bfl
                  << " mm, f/#=" << paraxial.fNumber << '\n';
        const auto before = optics::spot(system, catalog, system.fields[0], 9);
        optics::autofocus(system, catalog);          // изменяет расстояние до изображения
        const auto after = optics::spot(system, catalog, system.fields[0], 9);
        std::cout << "3. On-axis RMS: " << before.rms * 1000 << " -> "
                  << after.rms * 1000 << " um; image Z=" << system.imageZ() << " mm\n";

        const auto ray = optics::pupilRay(system, catalog, system.fields[0], wavelength, 0, .5);
        const auto path = optics::trace(system, catalog, ray);
        if (path.status != optics::TraceStatus::Complete)
            throw std::runtime_error("The demonstration ray did not reach the image");
        std::cout << "4. One ray (x,y,z in mm):\n";
        for (const auto& point : path.points)
            std::cout << "   " << point.x << ", " << point.y << ", " << point.z << '\n';
        std::cout << "   Optical path length=" << path.opl << " mm\n";

        optics::ParameterSolve curvature;
        curvature.surface = 1; curvature.reference = 0;
        curvature.kind = optics::SolveKind::CurvaturePickup;
        curvature.scale = -1; curvature.offset = -.005; // смещение кривизны, 1/мм
        optics::ParameterSolve edge;
        edge.parameter = optics::SolveParameter::Thickness;
        edge.kind = optics::SolveKind::EdgeThickness;
        edge.surface = 0; edge.reference = 1;
        edge.height = 10; edge.value = 1;             // край 1 мм на высоте 10 мм
        system.solves = {curvature, edge};
        optics::applySolves(system);                 // сначала кривизна, затем край
        requireValid();
        std::cout << "5. c2=-c1-0.005 1/mm: R2=" << system.surfaces[1].radius
                  << " mm; axial thickness=" << system.surfaces[0].thickness << " mm\n";
        optics::autofocus(system, catalog);
        const auto linkedSpot = optics::spot(system, catalog, system.fields[0], 9);
        std::cout << "6. Refocused linked lens: RMS=" << linkedSpot.rms * 1000
                  << " um; image Z=" << system.imageZ() << " mm\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
