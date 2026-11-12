#include "project.hpp"
#include "automation.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QStringConverter>
#include <iostream>

using namespace optics;
namespace {
int checks=0;
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
void near(double a,double b,double epsilon,const char* message) {check(std::abs(a-b)<epsilon,message);}
void rejects(const QByteArray& bytes,const char* message) {
    try {importZemax(bytes);} catch(const std::invalid_argument&){++checks;return;}
    throw std::runtime_error(message);
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        QFile input(QString::fromUtf8(OPTICS_SOURCE_DIR)+"/examples/zemax/singlet.zmx");
        check(input.open(QIODevice::ReadOnly),"fixture opens");
        // Git checkout may use CRLF on Windows. Keep text mutations below
        // independent of checkout line endings and exercise CRLF explicitly.
        auto bytes=input.readAll();
        bytes.replace("\r\n","\n");
        const auto imported=importZemax(bytes);
        auto crlf=bytes;crlf.replace("\n","\r\n");
        check(serializeProject(importZemax(crlf))==serializeProject(imported),"CRLF and LF prescriptions import identically");
        const auto& s=imported.system;
        check(s.surfaces.size()==2 && s.stop==0,"object/image removed and STOP mapped");
        check(s.wavelengths.size()==3 && s.primary==1,"inactive padded WAVM slot excluded despite positive weight");
        check(s.fields.size()==3 && s.fields.back().y==7,"FTYP field count excludes padding");
        near(s.pupilDiameter,10,1e-14,"ENPD diameter unchanged at infinity");
        near(s.surfaces[0].radius,50,1e-14,"curvature inverted to radius");
        near(s.surfaces[1].radius,-50,1e-14,"curvature sign retained");
        near(s.imageZ(),51.7,1e-12,"image distance includes last physical thickness");
        near(s.wavelengths[1].um,.5875618,1e-15,"Zemax wavelengths remain in um");
        // Independent thick-lens formula, not another importer round trip.
        const double n=imported.catalog.get("N-BK7").index(.5875618);
        const double power=(n-1)*(1./50-1./-50+(n-1)*5/(n*50*-50));
        near(paraxial(s,imported.catalog,.5875618).efl,1/power,1e-10,"EFL agrees with thick lens equation");
        const auto ray=trace(s,imported.catalog,pupilRay(s,imported.catalog,{0,0,1},.5875618,.2,.4));
        check(ray.status==TraceStatus::Complete,"imported real ray reaches image");
        const auto restored=deserializeProject(serializeProject(imported));
        check(serializeProject(restored)==serializeProject(imported),"native save retains prescription and import notes");
        auto changed=bytes;changed.replace("STOP\n","");changed.replace("SURF 2\n","SURF 2\nSTOP\n");
        check(importZemax(changed).system.stop==1,"internal stop is not converted into a non-refracting surface");
        changed=bytes;changed.replace("TYPE STANDARD\nCURV -0.02","TYPE EVENASPH\nPARM 1 0\nPARM 2 1e-7\nPARM 8 2e-24\nCURV -0.02");
        const auto asphere=importZemax(changed);
        near(asphere.system.surfaces[1].asphere[0],1e-7,1e-20,"PARM 2 maps to A4, not A6");
        near(asphere.system.surfaces[1].asphere[6],2e-24,1e-35,"PARM 8 maps to A16");
        changed.replace("PARM 1 0","PARM 1 1e-5");rejects(changed,"nonzero A2 cannot be dropped");
        changed=bytes;changed.replace("ENPD 10","FNUM 5 0");
        near(importZemax(changed).system.pupilDiameter,std::abs(1/power)/5,1e-10,"FNUM converted to EPD with infinite-conjugate EFL");
        changed=bytes;changed.replace("DISZ INFINITY","DISZ 200");changed.replace("FTYP 0","FTYP 1");
        const auto finite=importZemax(changed);
        check(finite.system.objectDistance==200 && finite.system.fieldType==FieldType::ObjectHeight,"finite object height preserved");
        changed.replace("STOP\n","");changed.replace("SURF 2\n","SURF 2\nSTOP\n");
        const double A=1-5/n*(n-1)/50, B=5/n;
        near(importZemax(changed).system.pupilDiameter,10*std::abs(A/(A+B/200)),1e-12,"finite-conjugate entrance pupil plane converted");
        for(auto encoding:{QStringConverter::Utf16LE,QStringConverter::Utf16BE}) {
            auto text=QString::fromUtf8(bytes);text.replace("Front",QString::fromUtf8("Передняя"));
            QStringEncoder encoder(encoding);
            const QByteArray encoded=encoder(text);
            for(bool bom:{false,true}) {
                const auto data=(bom?(encoding==QStringConverter::Utf16LE?QByteArray("\xff\xfe"):QByteArray("\xfe\xff")):QByteArray{})+encoded;
                check(importZemax(data).system.surfaces[0].name=="Передняя","UTF-16 endian/BOM detection preserves Cyrillic");
            }
        }
        check(importZemax(QByteArray("\xef\xbb\xbf")+bytes).system.surfaces.size()==2,"UTF-8 BOM accepted");
        const auto result=executeRequest({{"operation","import_zemax"},{"data",QString::fromUtf8(bytes)}});
        check(result["project"].isObject(),"batch API imports text without a preexisting project");
        const std::vector<std::pair<QByteArray,QByteArray>> invalid={
            {"MODE SEQ","MODE NSC"},{"UNIT MM","UNIT IN"},{"TYPE STANDARD","TYPE COORDBRK"},
            {"N-BK7","MISSING_GLASS"},{"N-BK7 0","N-BK7 1"},{"SURF 2","SURF 9"},
            {"PWAV 2","PWAV 4"},{"ENPD 10","ENPD nan"},{"CURV -0.02","CURV inf"},
            {"DIAM 12.5","DIAM -1"},{"DISZ 5\n","DISZ -2\n"},{"VCYN 0 0 0","VCYN 0 .1 0"},
            {"DISZ INFINITY","DISZ 0"},{"TOFF 0 0 0 0 0 0 0","TOFF 1 0 0 0 0 0 0"},
            {"ENVD 20 1","ENVD 25 1"},{"FTYP 0","FTYP 3"},{"GLAS N-BK7","GLAS MIRROR"},
            {"FLAP 0 12.5 0","FLAP 0 10 0"},{"FLAP 0 12.5 0","FLAP 1 12.5 0"}};
        for(const auto& [from,to]:invalid) {changed=bytes;changed.replace(from,to);rejects(changed,"unsupported/malformed prescription accepted");}
        for(const auto& extra:{"MNUM 2 1\n","COAT AR\n","PZUP 1 2 1 0\n","UNKNOWN 1\n","SQAP 2 2\n"})
            rejects(bytes+extra,"unsupported feature silently ignored");
        changed=bytes;changed.replace("STOP\n","");rejects(changed,"missing STOP accepted");
        rejects(bytes.left(bytes.indexOf("SURF 3")),"truncated image endpoint accepted");
        rejects({},"empty file accepted");
        changed=bytes;changed.replace("ENPD 10","FLOA");
        near(importZemax(changed).system.pupilDiameter,25,1e-12,"FLOA uses physical stop diameter");
        changed=bytes;changed.replace("TOL\nTOFF 0 0 0 0 0 0 0","TOL TOFF 0 0 0 0 0 0 0");
        check(importZemax(changed).system.surfaces.size()==2,"Zemax 13 compound tolerance header accepted");
        changed.replace("TOL TOFF 0 0 0 0 0 0 0","TOL TOFF 0 0 1 0 0 0 0");
        rejects(changed,"nonzero compound tolerance offsets cannot be discarded");
        changed=bytes;changed.replace("DISZ 46.7","DISZ -1000");
        const auto virtualImage=importZemax(changed);
        near(virtualImage.system.defocus,-1000,1e-12,"negative image interval retained as native defocus");
        check(!zemaxAnalysisLimitation(virtualImage).isEmpty(),"virtual image requires explicit analysis limitation");
        changed=bytes;changed.replace("FTYP 0 0 3 3 0 0 0","FTYP 0 0 3 3 0 0 1");
        check(!zemaxAnalysisLimitation(importZemax(changed)).isEmpty(),"afocal flag cannot silently become an ordinary image analysis");
        std::cout<<checks<<" Zemax checks passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
