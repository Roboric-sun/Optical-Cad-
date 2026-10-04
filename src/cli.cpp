#include "optics/model.hpp"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    try {
        optics::Catalog catalog;
        if (argc > 1 && std::string(argv[1]) == "--nonsequential") {
            auto scene = optics::Scene::demo();
            auto result = optics::traceScene(scene, catalog);
            std::cout << "Rays: " << result.launched
                      << "\nDetector power [W]: " << result.detectedPower
                      << "\nAbsorbed [W]: " << result.absorbedPower
                      << "\nEscaped [W]: " << result.escapedPower
                      << "\nTruncated [W]: " << result.truncatedPower << "\n";
            return 0;
        }
        auto system = optics::SequentialSystem::demo();
        optics::autofocus(system, catalog);
        auto p = optics::paraxial(system, catalog, system.wavelengths[system.primary].um);
        std::cout << std::setprecision(9) << "EFL [mm]: " << p.efl << "\nBFL [mm]: " << p.bfl
                  << "\nf/#: " << p.fNumber << "\nImage Z [mm]: " << system.imageZ() << "\n";
        for (auto field : system.fields) {
            auto spot = optics::spot(system, catalog, field);
            auto wave = optics::wavefront(system, catalog, field);
            std::cout << "Field [deg]: " << field.y << "  RMS spot [um]: " << spot.rms * 1000
                      << "  RMS OPD [waves]: " << wave.rms * 1000 / wave.wavelength << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
