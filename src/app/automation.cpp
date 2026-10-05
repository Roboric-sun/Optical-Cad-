#include "automation.hpp"
#include <QJsonArray>
#include <QJsonDocument>
using namespace optics;
namespace {
QJsonArray values(const std::vector<double>& v) { QJsonArray out; for(double x:v) out.append(x); return out; }
QJsonArray vector(Vec3 p) {return {p.x,p.y,p.z};}
int option(const QJsonObject& o,const char* name,int fallback,int minimum,int maximum) {
    if(!o.contains(name)) return fallback;
    const auto v=o[name];
    if(!v.isDouble() || v.toDouble()<minimum || v.toDouble()>maximum || std::floor(v.toDouble())!=v.toDouble())
        throw std::invalid_argument(std::string("Invalid integer option: ")+name);
    return v.toInt();
}
}
QJsonObject executeRequest(const QJsonObject& request) {
    const auto operation=request["operation"].toString();
    if(operation=="import_geopter") {
        auto project=importGeopter(QJsonDocument(request["data"].toObject()).toJson());
        return {{"project",QJsonDocument::fromJson(serializeProject(project)).object()}};
    }
    if(!request["project"].isObject()) throw std::invalid_argument("Request requires a project object");
    auto project=deserializeProject(QJsonDocument(request["project"].toObject()).toJson());
    auto& s=project.system;auto& c=project.catalog;
    const auto options=request["options"].toObject();
    QJsonObject result;
    if(operation=="validate") result["valid"]=true;
    else if(operation=="autofocus") result["image_z_mm"]=autofocus(s,c);
    else if(operation=="optimize") {
        auto plan=project.optimization.value_or(defaultOptimization(s,c));
        const auto optimized=optimize(s,c,plan);
        result={{"before",optimized.before},{"after",optimized.after},{"evaluations",double(optimized.evaluations)},
                {"history",values(optimized.history)}};
    } else if(operation=="analyze") {
        const int grid=option(options,"pupil_grid",s.pupilGrid,3,65);
        const auto para=paraxial(s,c,s.wavelengths[s.primary].um);
        result={{"efl_mm",para.efl},{"bfl_mm",para.bfl},{"f_number",para.fNumber},{"image_z_mm",s.imageZ(c)}};
        QJsonObject indices;
        for(const auto& surface:s.surfaces) indices[QString::fromStdString(surface.material)]=c.get(surface.material).index(s.wavelengths[s.primary].um);
        result["refractive_indices"]=indices;
        QJsonArray fields;
        for(auto field:s.fields) {
            const auto sp=spot(s,c,field,grid);
            QJsonObject item{{"x",field.x},{"y",field.y},{"unit",s.fieldType==FieldType::Angle?"deg":"mm"},
                {"rms_mm",sp.rms},{"centroid_mm",vector(sp.centroid)},{"launched",double(sp.launched)},
                {"survived",double(sp.samples.size())}};
            try {const auto wf=wavefront(s,c,field,grid);item["opd_rms_mm"]=wf.rms;item["opd_pv_mm"]=wf.pv;}
            catch(const std::exception& e){item["wavefront_error"]=QString::fromUtf8(e.what());}
            if (!sp.samples.empty()) {
                const auto energy=encircledEnergy(sp);
                item["encircled_radius_mm"]=values(energy.x); item["encircled_fraction"]=values(energy.y[0]);
            }
            fields.append(item);
        }
        result["fields"]=fields;
        const auto dist=distortion(s,c);result["distortion_percent"]=values(dist.y[0]);
        const auto curve=fieldCurvature(s,c);
        result["tangential_focus_mm"]=values(curve.y[0]);result["sagittal_focus_mm"]=values(curve.y[1]);
    } else if(operation=="diffraction") {
        const auto field=option(options,"field",0,0,int(s.fields.size())-1);
        const auto size=option(options,"size",64,32,128);
        const auto grid=option(options,"pupil_grid",size/2+1,9,65);
        const auto d=polychromaticDiffraction(s,c,s.fields[field],size,grid);
        result={{"size",d.size},{"pixel_um",d.pixelUm},{"psf",values(d.psf)},
                {"frequency_per_mm",values(d.frequency)},{"mtf_x",values(d.mtfX)},{"mtf_y",values(d.mtfY)}};
    } else if(operation=="trace_rays") {
        const auto system=resolvedSystem(s,c);
        const auto vertices=system.vertices();
        QJsonArray rays;
        std::vector<Vec3> pupils{{0,0,0},{0,.5,0},{0,1,0}};
        if(options.contains("pupil_grid")) {
            const int grid=option(options,"pupil_grid",9,3,65);pupils.clear();
            for(int y=0;y<grid;++y)for(int x=0;x<grid;++x) {
                Vec3 p{2.*x/(grid-1)-1,2.*y/(grid-1)-1,0};if(p.norm2()<=1+1e-12)pupils.push_back(p);
            }
        }
        if (options.contains("pupils")) {
            if (!options["pupils"].isArray()) throw std::invalid_argument("pupils must be an array");
            const auto list = options["pupils"].toArray();
            if (list.isEmpty() || list.size() > 15000) throw std::invalid_argument("Invalid pupil count");
            pupils.clear();
            for (const auto& value : list) {
                const auto point = value.toArray();
                if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble() ||
                    std::abs(point[0].toDouble()) > 2 || std::abs(point[1].toDouble()) > 2)
                    throw std::invalid_argument("Invalid pupil coordinate");
                pupils.push_back({point[0].toDouble(), point[1].toDouble(), 0});
            }
        }
        const size_t firstField=options.contains("field")?option(options,"field",0,0,int(system.fields.size())-1):0;
        const size_t lastField=options.contains("field")?firstField+1:system.fields.size();
        for(size_t fi=firstField;fi<lastField;++fi) for(auto pupil:pupils) {
            const auto launched=pupilRay(system,c,system.fields[fi],system.wavelengths[system.primary].um,pupil.x,pupil.y);
            const auto path=trace(system,c,launched,true,SIZE_MAX,false);
            QJsonArray points;
            for(size_t i=1;i<path.points.size();++i) {
                const auto point=path.points[i]-Vec3{0,0,i<=vertices.size()?vertices[i-1]:system.imageZ()};
                points.append(vector(point));
            }
            rays.append(QJsonObject{{"field",double(fi)},{"pupil_x",pupil.x},{"pupil_y",pupil.y},{"complete",path.status==TraceStatus::Complete},
                {"points",points},{"opl_mm",path.opl},{"origin_mm",vector(launched.origin)},
                {"direction",vector(launched.direction)},{"power",path.power},
                {"exit_point_mm",vector(path.exitPoint)},{"exit_direction",vector(path.exitDirection)},
                {"image_mm",vector(path.image)},{"exit_index",path.index}});
        }
        result["rays"]=rays;
    } else if(operation=="wavefront") {
        const auto field=option(options,"field",0,0,int(s.fields.size())-1);
        const auto grid=option(options,"pupil_grid",17,3,65);
        const auto wf=wavefront(s,c,s.fields[field],grid);
        QJsonArray samples;
        for(const auto& point:wf.samples) samples.append(QJsonObject{{"pupil_x",point.px},{"pupil_y",point.py},{"opd_mm",point.opd},{"power",point.power}});
        result={{"rms_mm",wf.rms},{"pv_mm",wf.pv},{"wavelength_um",wf.wavelength},{"samples",samples}};
    } else if(operation=="trace_scene") {
        const auto t=traceScene(project.scene,c);
        result={{"launched",double(t.launched)},{"launched_w",t.launchedPower},{"detected_w",t.detectedPower},
            {"absorbed_w",t.absorbedPower},{"escaped_w",t.escapedPower},{"truncated_w",t.truncatedPower}};
        QJsonArray detectors;
        for(const auto& d:t.detectors) {
            const auto& object=project.scene.objects[d.objectIndex];
            const auto stats=detectorStatistics(d,object.size.x,object.size.y);
            detectors.append(QJsonObject{{"object",double(d.objectIndex)},{"nx",d.nx},{"ny",d.ny},
                {"watts",values(d.watts)},{"power_w",stats.power},{"rms_mm",stats.rmsRadius},
                {"has_power",stats.hasPower},{"centroid_mm",vector(stats.centroid)}});
        }
        result["detectors"]=detectors;
    } else if(operation=="export_geopter") result["data"]=QJsonDocument::fromJson(exportGeopter(project)).object();
    else throw std::invalid_argument("Unknown operation: "+operation.toStdString());
    if(operation=="autofocus"||operation=="optimize")
        result["project"]=QJsonDocument::fromJson(serializeProject(project)).object();
    return result;
}
