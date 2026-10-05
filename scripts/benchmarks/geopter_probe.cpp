// Standalone probe compiled against an external, unmodified Geopter optical library.
// No Geopter source is linked into Optical CAD.
#include "system/optical_system.h"
#include "sequential/sequential_trace.h"
#include "spec/field_spec.h"
#include "spec/wavelength_spec.h"
#include "nlohmann/json.hpp"
#include <fstream>
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5)return 2;
    geopter::OpticalSystem system;
    system.Initialize();
    system.GetMaterialLib()->LoadAgfFiles({argv[3]});
    system.LoadFile(argv[1]);
    system.UpdateModel();
    auto* first=system.GetFirstOrderData();
    nlohmann::json result;
    result["efl_mm"]=first->effective_focal_length;
    result["bfl_mm"]=first->back_focal_length;
    const auto wave=system.GetOpticalSpec()->GetWavelengthSpec()->ReferenceWavelength();
    result["wavelength_nm"]=wave;
    geopter::SequentialTrace trace(&system);
    trace.SetApertureCheck(false);trace.SetApplyVig(true);
    result["rays"]=nlohmann::json::array();
    auto* fields=system.GetOpticalSpec()->GetFieldSpec();
    for(int fi=0;fi<fields->NumberOfFields();++fi) for(double pupil:{0.,.5,1.}) {
        auto ray=trace.CreatePupilRay({0,pupil},fields->GetField(fi),wave);
        nlohmann::json row;row["field"]=fi;row["pupil_y"]=pupil;row["status"]=int(ray->Status());
        row["points"]=nlohmann::json::array();
        for(int i=1;i<ray->NumberOfSegments();++i) {
            auto* p=ray->GetSegmentAt(i);
            row["points"].push_back({p->X(),p->Y(),p->Z()});
        }
        result["rays"].push_back(row);
    }
    if(argc==5) {
        nlohmann::json input; std::ifstream launch(argv[4]); launch>>input;
        result["matched_rays"]=nlohmann::json::array();
        for(const auto& row:input["rays"]) {
            const auto origin=row["origin_mm"].get<std::vector<double>>();
            const auto direction=row["direction"].get<std::vector<double>>();
            system.GetOpticalAssembly()->GetGap(0)->SetThickness(-origin[2]);
            system.UpdateModel();
            const auto path=trace.CreateSequentialPath(wave);
            auto ray=std::make_shared<geopter::Ray>(path.Size());
            trace.TraceRayThroughoutPath(ray,path,{origin[0],origin[1],0},{direction[0],direction[1],direction[2]});
            nlohmann::json item;item["status"]=int(ray->Status());item["points"]=nlohmann::json::array();
            double opl=0;
            for(int i=1;i<ray->NumberOfSegments();++i) {
                auto* p=ray->GetSegmentAt(i);item["points"].push_back({p->X(),p->Y(),p->Z()});
                if(i+1<ray->NumberOfSegments())opl+=p->OpticalPathLength();
            }
            item["opl_mm"]=opl;result["matched_rays"].push_back(item);
        }
    }
    std::ofstream file(argv[2]);file<<result.dump(2)<<"\n";
}
