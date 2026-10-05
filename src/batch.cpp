#include "automation.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QJsonObject response;
    int code=0;
    try {
        QFile input;
        if(argc>2) throw std::invalid_argument("Usage: optics_batch [request.json], otherwise JSON on stdin");
        if(argc==2) {input.setFileName(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open request");}
        QByteArray bytes;
        if(argc==2) bytes=input.read(32*1024*1024+1);
        else {
            char buffer[16384];
            size_t size;
            while((size=std::fread(buffer,1,sizeof(buffer),stdin))>0) {
                bytes.append(buffer,qsizetype(size));
                if(bytes.size()>32*1024*1024) break;
            }
            if(std::ferror(stdin)) throw std::runtime_error("Cannot read stdin");
        }
        if(bytes.size()>32*1024*1024) throw std::invalid_argument("Request exceeds 32 MB");
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError||!doc.isObject()) throw std::invalid_argument("Invalid request JSON");
        response={{"ok",true},{"result",executeRequest(doc.object())}};
    } catch(const std::exception& e) {response={{"ok",false},{"error",QString::fromUtf8(e.what())}};code=1;}
    const auto output=QJsonDocument(response).toJson(QJsonDocument::Compact)+"\n";
    std::fwrite(output.constData(),1,size_t(output.size()),stdout);
    return code;
}
