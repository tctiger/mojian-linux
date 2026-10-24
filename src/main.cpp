#include "editor.h"
#include "services.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QThread>
#include <cstdio>

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        const QString option = QString::fromLocal8Bit(argv[i]);
        if (option == "--version") { std::puts("Mojian Linux 1.0.0\nBuild: " MOJIAN_BUILD_TIME); return 0; }
        if (option == "--help") { std::puts("mojian [file.md ...]\nmojian --version\nF1: Chinese user guide"); return 0; }
    }
    QApplication app(argc, argv);
    Q_INIT_RESOURCE(resources);
    QApplication::setOrganizationName("Mojian"); QApplication::setApplicationName("mojian"); QApplication::setApplicationDisplayName("墨笺"); QApplication::setApplicationVersion("1.0.0"); app.setWindowIcon(QIcon(":/mojian/icon.png"));
    app.setDesktopFileName("io.mojian.Editor");
    Mojian::Services::installLocalization(&app);
    auto args = app.arguments(); args.removeFirst();
    if (args.contains("--version")) { std::puts("Mojian Linux 1.0.0\nBuild: " MOJIAN_BUILD_TIME); return 0; }
    if (args.contains("--help")) { std::puts("mojian [file.md ...]\nmojian --version\nF1: Chinese user guide"); return 0; }
    QJsonArray paths; for (auto path : args) if (!path.startsWith("--")) paths.append(QFileInfo(path).absoluteFilePath());
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation); if (runtime.isEmpty()) runtime = QDir::tempPath() + "/mojian-" + qEnvironmentVariable("USER"); QDir().mkpath(runtime);
    QString name = runtime + "/mojian-editor-v2";
    auto forward = [&] {
        QLocalSocket socket; socket.connectToServer(name); if (!socket.waitForConnected(300)) return false;
        socket.write(QJsonDocument(paths).toJson(QJsonDocument::Compact) + '\n'); socket.flush();
        if (!socket.waitForReadyRead(1500)) return false;
        return socket.readAll().startsWith("OK");
    };
    if (forward()) return 0;
    QLockFile lock(name + ".lock");
    if (!lock.tryLock(0)) { for (int i = 0; i < 10; ++i) { QThread::msleep(150); if (forward()) return 0; } std::fputs("Mojian is already starting; retry shortly.\n", stderr); return 1; }
    QLocalServer::removeServer(name); QLocalServer server; server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(name)) { std::fprintf(stderr, "Cannot start local server: %s\n", qPrintable(server.errorString())); return 1; }
    auto openPaths = [](const QJsonArray &files) {
        if (files.isEmpty()) { auto window = new Mojian::Window; window->show(); window->raise(); window->activateWindow(); }
        else for (const auto &value : files) Mojian::Window::openDocument(value.toString());
    };
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (auto socket = server.nextPendingConnection()) {
            auto data = QSharedPointer<QByteArray>::create();
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, data, openPaths] {
                data->append(socket->readAll()); if (data->size() > 1024 * 1024) { socket->disconnectFromServer(); return; }
                if (!data->endsWith('\n')) return;
                QJsonParseError error; auto message = QJsonDocument::fromJson(*data, &error);
                if (error.error == QJsonParseError::NoError && message.isArray()) { openPaths(message.array()); socket->write("OK\n"); socket->flush(); }
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    bool restored=false;
    if(paths.isEmpty()){
        QDir recovery(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recovery");auto entries=recovery.entryList({"*.json"},QDir::Files,QDir::Time);
        if(!entries.isEmpty()&&QMessageBox::question(nullptr,"恢复文档",QString("发现 %1 份上次未正常关闭的文档恢复记录，是否恢复？").arg(entries.size()),QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)==QMessageBox::Yes){
            for(auto name:entries.mid(0,30)){QFile file(recovery.filePath(name));if(!file.open(QIODevice::ReadOnly)||file.size()>64000000)continue;auto data=QJsonDocument::fromJson(file.readAll()).object();if(!data.contains("text"))continue;auto window=new Mojian::Window;window->recoveryKey=QFileInfo(name).completeBaseName();window->filePath=data.value("path").toString();window->lineEnding=data.value("lineEnding").toString("\n");window->editor->setPlainText(data.value("text").toString());window->editor->document()->setModified(true);QFile disk(window->filePath);if(disk.open(QIODevice::ReadOnly))window->diskHash=QCryptographicHash::hash(disk.readAll(),QCryptographicHash::Sha256);if(!window->filePath.isEmpty())window->watcher.addPath(window->filePath);window->show();file.close();restored=true;}
        }
    }
    if(!restored)openPaths(paths);
    return app.exec();
}
