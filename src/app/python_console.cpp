#include "python_console.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#endif
PythonConsole::PythonConsole(Project project,std::function<void(Project)> apply,QWidget* parent)
    :QDialog(parent),project_(std::move(project)),apply_(std::move(apply)) {
    setObjectName("pythonConsole");setWindowTitle("Python — Optical CAD");resize(900,650);
    auto* layout=new QVBoxLayout(this);
    auto* info=new QLabel("Python 3 запускается отдельным процессом. Доступны project, print и стандартная библиотека. "
        "Изменения проекта применяются одной операцией отмены после успешного выполнения. «Остановить» отменяет применение.");
    info->setWordWrap(true);layout->addWidget(info);
    interpreter_=new QLineEdit(qEnvironmentVariable("OPTICALCAD_PYTHON",QStandardPaths::findExecutable("python3")));
    if(interpreter_->text().isEmpty()) interpreter_->setText(QStandardPaths::findExecutable("python"));
    interpreter_->setObjectName("pythonInterpreter");interpreter_->setPlaceholderText("Полный путь к Python 3");layout->addWidget(interpreter_);
    source_=new QPlainTextEdit;source_->setObjectName("pythonSource");
    source_->setPlainText("print(project.analyze())\n# project.system['surfaces'][0]['radius'] = 60\n# project.autofocus()\n");layout->addWidget(source_,2);
    output_=new QPlainTextEdit;output_->setReadOnly(true);output_->setObjectName("pythonOutput");
    output_->setMaximumBlockCount(3000);layout->addWidget(output_,1);
    auto* buttons=new QHBoxLayout;run_=new QPushButton("Выполнить");run_->setObjectName("pythonRun");
    stop_=new QPushButton("Остановить");stop_->setObjectName("pythonStop");stop_->setEnabled(false);
    buttons->addWidget(run_);buttons->addWidget(stop_);layout->addLayout(buttons);
    connect(run_,&QPushButton::clicked,this,[this]{run();});
    connect(stop_,&QPushButton::clicked,this,[this]{stopped_=true;stopProcess();});
#ifdef Q_OS_UNIX
    process_.setChildProcessModifier([] { ::setsid(); });
#elif defined(Q_OS_WIN)
    connect(&process_,&QProcess::started,this,[this]{
        auto job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        auto child=OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE,FALSE,DWORD(process_.processId()));
        if(!job || !child || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info)) ||
           !AssignProcessToJobObject(job,child)) {
            if(child)CloseHandle(child);if(job)CloseHandle(job);
            stopped_=true;process_.kill();output_->appendPlainText("Cannot manage the Python process tree");return;
        }
        CloseHandle(child);processJob_=job;
    });
#endif
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{
        const auto bytes=process_.readAllStandardOutput();
        output_->appendPlainText(QString::fromUtf8(bytes.left(1024*1024)));
    });
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){
        output_->appendPlainText(process_.errorString());
        if(process_.state()==QProcess::NotRunning){run_->setEnabled(true);stop_->setEnabled(false);}
    });
    connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){
#ifdef Q_OS_WIN
        if(processJob_){CloseHandle(processJob_);processJob_=nullptr;}
#endif
        run_->setEnabled(true);stop_->setEnabled(false);
        if(stopped_){output_->appendPlainText("Остановлено. Проект не изменён.");return;}
        if(code!=0||status!=QProcess::NormalExit){output_->appendPlainText("Скрипт завершился с ошибкой. Проект не изменён.");return;}
        try {
            auto result=loadProject(directory_.filePath("result.optcad"));
            apply_(result);project_=std::move(result);
            output_->appendPlainText("Выполнено. Результат применён.");
        }catch(const std::exception& e){output_->appendPlainText(QString::fromUtf8(e.what()));}
    });
}
void PythonConsole::stopProcess(){
    if(process_.state()==QProcess::NotRunning)return;
#ifdef Q_OS_UNIX
    const auto pid=process_.processId();
    if(pid>0)::kill(-pid,SIGKILL);
#elif defined(Q_OS_WIN)
    if(processJob_){CloseHandle(processJob_);processJob_=nullptr;}
#endif
    process_.kill();
}
PythonConsole::~PythonConsole(){disconnect(&process_,nullptr,this,nullptr);stopProcess();process_.waitForFinished(3000);}
void PythonConsole::run(){
    if(process_.state()!=QProcess::NotRunning)return;
    try {
        if(!directory_.isValid())throw std::runtime_error("Cannot create script directory");
        const QDir app(QCoreApplication::applicationDirPath());
        QString sdk;
        for(const auto& path:{app.filePath("../Resources/python"),app.filePath("../../../scripts"),app.filePath("../scripts"),QString(OPTICS_SCRIPT_DIR)})
            if(QFileInfo::exists(path+"/opticalcad.py")){sdk=QDir(path).absolutePath();break;}
        if(sdk.isEmpty())throw std::runtime_error("Python API module is missing");
        QString batch=app.filePath("optics_batch");
#ifdef Q_OS_WIN
        batch+=".exe";
#endif
        if(!QFileInfo::exists(batch))batch=app.filePath("../../../optics_batch");
        if(!QFileInfo::exists(batch))throw std::runtime_error("optics_batch executable is missing");
        saveProject(directory_.filePath("input.optcad"),project_);
        QFile::remove(directory_.filePath("result.optcad"));
        auto write=[&](const QString& path,const QByteArray& data){QFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size())throw std::runtime_error("Cannot write script");};
        write(directory_.filePath("user.py"),source_->toPlainText().toUtf8());
        write(directory_.filePath("runner.py"),
            "import sys, os\nfrom pathlib import Path\nsys.path.insert(0, os.environ['OPTICALCAD_SDK'])\n"
            "from opticalcad import Project\nbase = Path(__file__).parent\n"
            "project = Project.load(base / 'input.optcad')\n"
            "exec(compile((base / 'user.py').read_text(encoding='utf-8'), '<console>', 'exec'))\n"
            "project.save(base / 'result.optcad')\n");
        auto env=QProcessEnvironment::systemEnvironment();env.insert("OPTICALCAD_BATCH",QFileInfo(batch).absoluteFilePath());
        env.insert("OPTICALCAD_SDK",sdk);env.insert("PYTHONIOENCODING","utf-8");process_.setProcessEnvironment(env);
        stopped_=false;output_->clear();run_->setEnabled(false);stop_->setEnabled(true);
        process_.start(interpreter_->text(),{"-u",directory_.filePath("runner.py")});
    }catch(const std::exception& e){output_->appendPlainText(QString::fromUtf8(e.what()));}
}
