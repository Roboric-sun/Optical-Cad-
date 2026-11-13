#include "project.hpp"
#include <QMap>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <algorithm>

using namespace optics;
namespace {
[[noreturn]] void fail(const QString& message) {
    throw std::invalid_argument(("Zemax ZMX: " + message).toStdString());
}
double numeric(const QStringList& args, qsizetype index, const QString& record) {
    bool ok = false;
    const double v = args.value(index).toDouble(&ok);
    if (!ok || !std::isfinite(v)) fail("invalid number in " + record);
    return v;
}
int integer(const QStringList& args, qsizetype index, int low, int high, const QString& record) {
    const double v = numeric(args,index,record);
    if (v < low || v > high || std::floor(v) != v) fail("invalid index/count in " + record);
    return int(v);
}
struct Row {
    Surface surface;
    QString type = "STANDARD";
    QMap<int,double> parameters;
    bool curvature=false, thickness=false, diameter=false, infinite=false;
    std::optional<double> clearRadius;
};
// Our finite-conjugate pupil is referenced to the first vertex. Zemax's EPD
// is at the paraxial entrance pupil; transfer it through the pre-stop ABCD.
double pupilScale(const SequentialSystem& s, const Catalog& c) {
    if (s.objectDistance == 0) return 1;
    double A=1,B=0,C=0,D=1,n=1;
    for (size_t i=0;i<s.stop;++i) {
        const auto& a=s.surfaces[i];
        const double next=c.get(a.material).index(s.wavelengths[s.primary].um);
        const double power=a.radius==0?0:(next-n)/a.radius;
        C-=power*A; D-=power*B; n=next;
        A+=a.thickness/n*C; B+=a.thickness/n*D;
    }
    if (std::abs(A)<1e-12 || std::abs(A+B/s.objectDistance)<1e-12)
        fail("entrance pupil at a singular conjugate");
    return std::abs(A/(A+B/s.objectDistance));
}
}

Project importZemax(const QByteArray& bytes, const Catalog& catalog) {
    if (bytes.isEmpty() || bytes.size()>32*1024*1024) fail("empty file or file exceeds 32 MB");
    // Zemax files occur as ASCII/UTF-8 and UTF-16 with a BOM. Also recognize
    // BOM-less UTF-16 by the first ASCII keyword, not by the host byte order.
    QStringDecoder decoder(bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff")
        ? QStringConverter::Utf16 : bytes.size()>1 && bytes[1]=='\0'
        ? QStringConverter::Utf16LE : bytes[0]=='\0' ? QStringConverter::Utf16BE : QStringConverter::Utf8);
    QString text=decoder.decode(bytes);
    if (decoder.hasError()) fail("invalid text encoding (use ASCII, UTF-8 or UTF-16)");
    if (text.startsWith(QChar(0xfeff))) text.remove(0,1);
    if (text.contains(QChar(0))) fail("binary file; save the lens as .ZMX");
    Project p; p.catalog=catalog;
    auto& s=p.system;
    s.surfaces.clear(); s.fields.clear(); s.wavelengths.clear(); s.solves.clear();
    s.defocus=0; s.objectDistance=0; s.primary=0;
    std::vector<Row> rows;
    QMap<QString,std::vector<double>> fieldData;
    QMap<int,Wavelength> waves;
    int fieldCount=-1,waveCount=-1,primary=1,stop=-1;
    bool mode=false,units=false,aperture=false,fieldType=false;
    double fNumber=0;
    bool floatByStop=false,afocal=false,meritSection=false;
    QJsonArray warnings,sourceSurfaceNumbers;
    auto warn=[&](const QString& message){if(!warnings.contains(message))warnings.append(message);};
    // These records describe display, analysis defaults or cached results.
    // Everything else must be interpreted explicitly or rejected.
    const QSet<QString> metadata={"VERS","NOTE","PFIL","LANG","GCAT","RAIM","PUSH",
        "SDMA","OMMA","ROPD","PICB","POLS","GSTD","NSCD","COFN","GLRS",
        "EFFL","PUPD","HIDE","SLAB","MEMA","SCOL","POPS","VDSZ","FIMP"};
    int lineNumber=0;
    for (const auto& raw:text.split('\n')) {
        ++lineNumber;
        const auto line=raw.trimmed();
        if (line.isEmpty() || line.startsWith('!')) continue;
        const auto tokens=line.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        const auto key=tokens.front().toUpper();
        const auto args=tokens.mid(1);
        const auto context=key+" at line "+QString::number(lineNumber);
        if (key=="MODE") { if(args.value(0)!="SEQ") fail("only sequential MODE SEQ is supported"); mode=true; }
        else if (key=="UNIT") { if(args.value(0)!="MM") fail("only millimetre UNIT MM is supported"); units=true; }
        else if (key=="NAME") s.name=line.mid(4).trimmed().toStdString();
        else if (key=="ENPD") { if(aperture)fail("multiple aperture specifications"); s.pupilDiameter=numeric(args,0,context); aperture=true; }
        else if (key=="FNUM") {
            if(aperture)fail("multiple aperture specifications");
            fNumber=numeric(args,0,context);
            if(fNumber<=0 || (args.size()>1 && integer(args,1,0,1,context)!=0))fail("only positive infinite-conjugate FNUM is supported");
            aperture=true;
        }
        else if(key=="FLOA") {
            if(aperture || !args.isEmpty())fail("invalid FLOA aperture specification");
            floatByStop=true;aperture=true;
        }
        else if (key=="FTYP") {
            s.fieldType=FieldType(integer(args,0,0,2,context));
            if(args.size()<4) fail("FTYP must declare field and wavelength counts");
            if(integer(args,1,0,1,context)!=0)fail("radially normalized fields are unsupported");
            fieldCount=integer(args,2,1,50,context); waveCount=integer(args,3,1,20,context); fieldType=true;
            if(args.size()>6)afocal=integer(args,6,0,1,context)!=0;
        }
        else if (key=="XFLN" || key=="YFLN" || key=="FWGN") {
            if(fieldData.contains(key)) fail("duplicate "+context);
            auto& values=fieldData[key];
            for(qsizetype i=0;i<args.size();++i)values.push_back(numeric(args,i,context));
        }
        else if (key=="VDXN" || key=="VDYN" || key=="VCXN" || key=="VCYN" || key=="VANN" || key=="GFAC" || key=="HYPR" || key=="ISNA") {
            for(qsizetype i=0;i<args.size();++i)if(numeric(args,i,context)!=0)fail("nonzero "+context+" is unsupported");
        }
        else if (key=="ENVD") {
            if(numeric(args,0,context)!=20 || numeric(args,1,context)!=1)fail("only 20 C / 1 atm environment is supported");
            for(qsizetype i=2;i<args.size();++i)if(numeric(args,i,context)!=0)fail("unsupported environment option");
        }
        else if (key=="WAVM") {
            int index=integer(args,0,1,1000,context);
            if(waves.contains(index))fail("duplicate wavelength slot");
            waves[index]={numeric(args,1,context),numeric(args,2,context)};
        }
        else if (key=="PWAV") primary=integer(args,0,1,20,context);
        else if (key=="MNUM") {
            if(integer(args,0,1,1000,context)!=1)fail("multiple configurations are unsupported");
            if(args.size()>1 && integer(args,1,1,1000,context)!=1)fail("unsupported current configuration");
        }
        else if(key=="BLNK") continue;
        else if(key=="TOL") {
            if(!args.isEmpty()) {
                if(args.front()!="TOFF")fail("unsupported "+context);
                for(qsizetype i=1;i<args.size();++i)if(numeric(args,i,context)!=0)fail("nonzero tolerance offset is unsupported");
            }
        }
        else if(key=="DMFS" || key=="TRAC") {
            if(key=="DMFS")meritSection=true;
            else if(!meritSection)fail("TRAC requires a merit function section");
            for(qsizetype i=0;i<args.size();++i)numeric(args,i,context);
            warn("Функция качества Zemax не перенесена; импортируется сохранённая геометрия.");
        }
        else if(key=="CSUM") {
            integer(args,0,0,2147483647,context);
        }
        else if(key=="TOFF" || key=="PUSH") {
            for(qsizetype i=0;i<args.size();++i)if(numeric(args,i,context)!=0)fail("nonzero "+context+" is unsupported");
        }
        else if(key=="MOFF") {
            // Empty, single-configuration settings saved by Zemax 13.
            const QStringList defaults={"0","1","\"\"","0","0","0","1","1","0","0.0","\"\""};
            if(args!=defaults)fail("non-default configuration settings are unsupported");
        }
        else if (key=="SURF") {
            if(integer(args,0,0,501,context)!=int(rows.size()))fail("surface indices must be contiguous from zero");
            rows.emplace_back(); rows.back().surface.name="Surface "+std::to_string(rows.size()-1);
        }
        else if(metadata.contains(key)) continue;
        else {
            if(rows.empty())fail("unsupported "+context);
            auto& row=rows.back(); auto& a=row.surface;
            if(key=="TYPE") {
                row.type=args.value(0);
                if(row.type!="STANDARD" && row.type!="EVENASPH")fail("unsupported surface type "+row.type+" at surface "+QString::number(rows.size()-1));
            }
            else if(key=="COMM") a.name=line.mid(4).trimmed().toStdString();
            else if(key=="CURV") {const double c=numeric(args,0,context);a.radius=c==0?0:1/c;row.curvature=true;}
            else if(key=="DISZ") {
                row.infinite=args.value(0).compare("INFINITY",Qt::CaseInsensitive)==0;
                a.thickness=row.infinite?0:numeric(args,0,context);row.thickness=true;
            }
            else if(key=="DIAM") {a.semiDiameter=numeric(args,0,context);row.diameter=true;}
            else if(key=="CONI") a.conic=numeric(args,0,context);
            else if(key=="CHZH") {
                if(args.size()!=2 || numeric(args,0,context)!=0 || numeric(args,1,context)!=0)
                    fail("only cached zero chief-ray-height CHZH is supported");
                warn("CHZH: сохранённая толщина перенесена как фиксированная; solve положения зрачка не пересчитывается после правок.");
            }
            else if(key=="PARM") {
                int index=integer(args,0,1,1000,context);
                if(row.parameters.contains(index))fail("duplicate parameter");
                row.parameters[index]=numeric(args,1,context);
            }
            else if(key=="GLAS") {
                // Model glasses use different dispersion laws; nd/Vd alone is
                // deliberately not substituted for a missing real catalog.
                if(integer(args,1,0,1,context)!=0)fail("model/substitute glass is unsupported; use a catalog glass");
                a.material=args.value(0).toStdString();
                if(a.material=="MIRROR")fail("reflecting systems are unsupported");
                p.catalog.get(a.material);
            }
            else if(key=="STOP") {if(stop!=-1)fail("multiple STOP surfaces");stop=int(rows.size()-1);}
            else if(key=="COAT" && (args.isEmpty() || args.value(0)=="NONE")) continue;
            else if(key=="CLAP" || key=="FLAP") {
                if(numeric(args,0,context)!=0)fail("annular aperture is unsupported");
                const double radius=numeric(args,1,context);
                if(radius<=0)fail("invalid circular aperture");
                // DIAM and CLAP may differ; retain the stricter clear aperture.
                row.clearRadius=radius;
                for(qsizetype i=2;i<args.size();++i)if(numeric(args,i,context)!=0)fail("offset aperture is unsupported");
            }
            else if(key=="MIRR") {
                // MIRR controls the layout drawing, not the optical medium.
                for(qsizetype i=0;i<args.size();++i)numeric(args,i,context);
            }
            else fail("unsupported "+context+" (surface "+QString::number(rows.size()-1)+")");
        }
    }
    if(!mode || !units || !aperture || !fieldType)fail("required MODE, UNIT, aperture or FTYP record is missing");
    if(rows.size()<3)fail("object, physical surface and image are required");
    if(stop<1 || stop>=int(rows.size())-1)fail("STOP must identify a physical surface");
    s.stop=size_t(stop-1);
    for(const auto& key:{QString("XFLN"),QString("YFLN"),QString("FWGN")})
        if(!fieldData.contains(key) || fieldData[key].size()<size_t(fieldCount))fail("missing/truncated "+key);
    for(int i=0;i<fieldCount;++i)s.fields.push_back({fieldData["XFLN"][i],fieldData["YFLN"][i],fieldData["FWGN"][i]});
    for(int i=1;i<=waveCount;++i) {
        if(!waves.contains(i))fail("missing active WAVM slot "+QString::number(i));
        s.wavelengths.push_back(waves[i]);
    }
    if(primary>waveCount)fail("PWAV is outside the active wavelength slots");
    s.primary=size_t(primary-1);
    // A zero-distance object can coincide with a dummy air plane. Move the
    // native first vertex downstream without inventing an infinite conjugate.
    size_t firstPhysical=1;
    double objectDistance=rows.front().surface.thickness;
    if(!rows.front().infinite && objectDistance==0 && s.fieldType==FieldType::ObjectHeight) {
        const auto& dummy=rows[1];const auto& a=dummy.surface;
        if(stop==1 || dummy.type!="STANDARD" || a.radius!=0 || a.conic!=0 || a.material!="AIR" ||
           !dummy.parameters.empty() || dummy.infinite || a.thickness<=0 || !dummy.diameter)
            fail("zero-distance object requires a removable plane in air followed by a positive gap");
        for(const auto& f:s.fields)if(std::hypot(f.x,f.y)>a.semiDiameter)fail("object field falls outside the coincident dummy aperture");
        firstPhysical=2;objectDistance=a.thickness;--s.stop;
        warn("Вспомогательная плоскость на объекте исключена; начало координат перенесено к следующей поверхности, расстояние объекта сохранено.");
    }
    for(size_t i=0;i<rows.size();++i) {
        auto& row=rows[i];auto& a=row.surface;
        if(row.clearRadius && (!row.diameter || *row.clearRadius!=a.semiDiameter))fail("different CLAP and DIAM radii are unsupported");
        if(!row.curvature || !row.thickness)fail("missing CURV/DISZ at surface "+QString::number(i));
        for(auto it=row.parameters.cbegin();it!=row.parameters.cend();++it) {
            if(it.value()==0)continue;
            // EVENASPH parameter 1 is r^2, 2 is r^4, ... 8 is r^16.
            if(row.type!="EVENASPH" || it.key()<2 || it.key()>8)fail("unsupported nonzero PARM at surface "+QString::number(i));
            a.asphere[size_t(it.key()-2)]=it.value();
        }
        if(i==0 || i+1==rows.size()) {
            if(a.radius!=0 || a.conic!=0 || row.type!="STANDARD" || a.material!="AIR" ||
               std::any_of(a.asphere.begin(),a.asphere.end(),[](double v){return v!=0;}))fail("curved or non-air object/image endpoint is unsupported");
            if(i==0) {
                // Older files use 1e10 in place of the literal INFINITY.
                s.objectDistance=row.infinite || objectDistance>=1e10?0:objectDistance;
                if(!row.infinite && objectDistance<1e10 && objectDistance<=0)fail("object must be at positive distance or INFINITY");
            } else if(row.infinite || a.thickness!=0) fail("image DISZ must be zero");
            continue;
        }
        if(i<firstPhysical)continue;
        if(row.infinite || !row.diameter || a.semiDiameter<=0)fail("finite DISZ and positive DIAM are required at surface "+QString::number(i));
        if(a.thickness<0 && i+2==rows.size()) {
            s.defocus=a.thickness;a.thickness=0;
            warn("Виртуальное изображение: отрицательный последний промежуток сохранён в defocus; текущая трассировка CAD не рассчитывает обратную проекцию к этому изображению.");
        }
        s.surfaces.push_back(a);
        sourceSurfaceNumbers.append(int(i));
    }
    if(fNumber>0) {
        const double efl=paraxial(s,p.catalog,s.wavelengths[s.primary].um).efl;
        s.pupilDiameter=std::abs(efl)/fNumber;
    }
    if(floatByStop) {
        double A=1,B=0,C=0,D=1,n=1;
        for(size_t i=0;i<s.stop;++i) {
            const auto& a=s.surfaces[i];const double next=p.catalog.get(a.material).index(s.wavelengths[s.primary].um);
            const double power=a.radius==0?0:(next-n)/a.radius;
            C-=power*A;D-=power*B;n=next;A+=a.thickness/n*C;B+=a.thickness/n*D;
        }
        const double magnification=std::abs(A+(s.objectDistance>0?B/s.objectDistance:0));
        if(magnification<1e-12)fail("FLOA stop at a pupil singularity");
        s.pupilDiameter=2*s.surfaces.at(s.stop).semiDiameter/magnification;
        warn("FLOA преобразована в фиксированный диаметр зрачка на первичной длине волны; это определение не пересчитывается автоматически после изменения схемы.");
    } else s.pupilDiameter*=pupilScale(s,p.catalog);
    const auto errors=s.validate(p.catalog);
    if(!errors.empty())fail(QString::fromStdString(errors.front()));
    p.mode=0;
    if(afocal)warn("Афокальное пространство Zemax: CAD показывает геометрию; анализы в мм не эквивалентны угловым анализам Zemax. Угловые OPD/PSF/MTF не поддерживаются.");
    p.workspace["zemaxImport"]=QJsonObject{{"afocalImageSpace",afocal},{"virtualImage",s.defocus<0},
        {"floatByStop",floatByStop},{"sourceSurfaceNumbers",sourceSurfaceNumbers},{"warnings",warnings}};
    p.workspace["importNotes"]="Zemax ZMX: centered sequential prescription imported in mm/um. "
        "Surface numbers exclude object/image; STOP is retained as the system stop. "
        "DIAM is used as a fixed clear semi-diameter. Display/analysis settings are not imported. "
        "Scalar Optical CAD tracing does not reproduce Zemax polarization or all analysis conventions.";
    return p;
}

QString zemaxAnalysisWarning(const Project& p) {
    const auto imported=p.workspace["zemaxImport"].toObject();
    if(imported.isEmpty() && !p.workspace["importNotes"].toString().startsWith("Zemax ZMX:"))return {};
    const QString warning="ТЕСТОВЫЙ РАСЧЁТ ZEMAX: результаты могут быть неточными. Сравните их с Zemax; расчёты выполняются алгоритмами Optical CAD.";
    if(imported["virtualImage"].toBool())
        return warning+" Виртуальное изображение: обратная проекция лучей пока не реализована; возможны отсутствие данных и ошибки расчёта.";
    if(imported["afocalImageSpace"].toBool())
        return warning+" Афокальная система: значения в мм не эквивалентны угловым характеристикам Zemax.";
    return warning;
}
