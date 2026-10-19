#include "project.hpp"

Project gullstrandEyeProject() {
    using namespace optics;
    Project p;
    // Gullstrand No. 1, relaxed: Blaker, JOSA 70 (1980), Tables I/II.
    // Indices are constant: no dispersion model is inferred from single-line data.
    for (auto [name, index] : {std::pair{"EYE_CORNEA",1.376}, {"EYE_AQUEOUS",1.336},
                              {"EYE_CORTEX",1.386}, {"EYE_NUCLEUS",1.406},
                              {"EYE_VITREOUS",1.336}}) {
        Material material; material.name = name; material.nd = index; material.vd = 0;
        p.catalog.add(material);
    }
    auto& s = p.system;
    s.name = "Глаз Гульстранда № 1 — без аккомодации";
    s.surfaces.clear();
    auto surface = [&](const char* name, double radius, double thickness, double aperture, const char* material) {
        Surface a; a.name=name; a.radius=radius; a.thickness=thickness;
        a.semiDiameter=aperture; a.material=material; s.surfaces.push_back(a);
    };
    surface("Роговица · передняя",7.7,.5,5.5,"EYE_CORNEA");
    surface("Роговица · задняя",6.8,3.1,5.5,"EYE_AQUEOUS");
    surface("Радужка · STOP",0,0,1.6,"EYE_AQUEOUS");
    s.surfaces.back().kind=SurfaceKind::Stop; s.stop=2;
    surface("Хрусталик · передняя кора",10,.546,5,"EYE_CORTEX");
    surface("Ядро · передняя",7.911,2.419,5,"EYE_NUCLEUS");
    surface("Ядро · задняя",-5.76,.635,5,"EYE_CORTEX");
    surface("Хрусталик · задняя кора",-6,17.187,5,"EYE_VITREOUS");
    s.pupilDiameter=3; s.objectDistance=0; s.defocus=0; s.pupilGrid=17;
    s.fields={{0,0,1},{0,2.5,1},{0,5,1}};
    s.wavelengths={{.4861327,1},{.5875618,1},{.6562725,1}}; s.primary=1;
    p.workspace["layoutRays"]=17;
    p.workspace["polychromaticDiffraction"]=true;
    return p;
}
