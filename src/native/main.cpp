#include "codex_monitor.h"
#include "background_monitor.h"
#include "clock.h"
#include "controller.h"
#include "main_window.h"
#include "simulation.h"
#include "windows_shutdown.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QCryptographicHash>
#include <cstdio>
#include <memory>

using namespace csa;
static QJsonObject compactStatus(const Controller &controller, const QString &scope) {
    const auto view = controller.view();
    return {{"appId", "codex-shutdown-automation"}, {"version", "0.2.0"}, {"scope", scope},
            {"phase", view.policy.phase}, {"armed", view.policy.armed}, {"healthy", view.monitor.healthy},
            {"threads", view.monitor.threads.size()}, {"blockers", view.monitor.blockers.size()},
            {"simulation", view.simulation}, {"startupEnabled", view.startupEnabled}};
}
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Codex Shutdown Automation");
    app.setOrganizationName("CodexShutdownAutomation");
    app.setApplicationVersion("0.2.0");
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({QCommandLineOption("background", "Start in the tray, with shutdown OFF."),
                       QCommandLineOption("simulation", "Use synthetic chats and a recording shutdown adapter."),
                       QCommandLineOption("status", "Print compact status of the running native utility."),
                       QCommandLineOption("quit", "Disarm and quit the running native utility.")});
    parser.process(app);
    const bool simulation = parser.isSet("simulation");
    const auto profile = QDir::cleanPath(QDir::homePath());
    const auto rawCodexHome = qEnvironmentVariable("CODEX_HOME", profile + "/.codex");
    if (!QDir::isAbsolutePath(rawCodexHome)) {
        QMessageBox::critical(nullptr, app.applicationName(), "CODEX_HOME must be an absolute path."); return 1;
    }
    const QString codexHome = QDir::cleanPath(QDir::fromNativeSeparators(rawCodexHome));
    const QString scope = QString::fromLatin1(QCryptographicHash::hash(
        (profile.toLower() + "|" + codexHome.toLower()).toUtf8(), QCryptographicHash::Sha256).toHex());
    const auto localData = qEnvironmentVariable("LOCALAPPDATA", QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    const QString directory = localData + "/CodexShutdownAutomation" + (simulation ? "/simulation-native" : "");
    QDir().mkpath(directory);
    const QString serverName = "codex-shutdown-native-" + QString::fromLatin1(QCryptographicHash::hash(
        directory.toLower().toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    QLocalSocket client;
    client.connectToServer(serverName);
    if (client.waitForConnected(750)) {
        const QString command = parser.isSet("quit") ? "quit" : parser.isSet("status") ? "status" : "show";
        client.write(QJsonDocument(QJsonObject{{"command", command}, {"scope", scope}}).toJson(QJsonDocument::Compact) + '\n');
        client.waitForBytesWritten(1000);
        QByteArray response;
        while (!response.contains('\n') && response.size() < 8192 && client.waitForReadyRead(5000)) response += client.readAll();
        if (response.isEmpty()) { std::fprintf(stderr, "The running utility did not respond.\n"); return 1; }
        std::fwrite(response.constData(), 1, response.size(), stdout);
        return QJsonDocument::fromJson(response).object().contains("error") ? 1 : 0;
    }
    if (parser.isSet("status") || parser.isSet("quit")) {
        std::fprintf(stderr, "The native utility is not running.\n"); return parser.isSet("quit") ? 0 : 1;
    }
    QLockFile lock(directory + "/instance.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        QMessageBox::warning(nullptr, app.applicationName(), "The monitor is already starting. Open the Desktop shortcut again shortly."); return 1;
    }
    try {
        Store store(directory + "/state");
        const auto clock = makeSteadyClock();
        std::unique_ptr<Monitor> monitor;
        std::unique_ptr<Shutdown> shutdown;
        if (simulation) {
            monitor = std::make_unique<SimulationMonitor>(clock());
            shutdown = std::make_unique<RecordingShutdown>();
        } else {
            monitor = std::make_unique<BackgroundMonitor>(std::make_unique<CodexMonitor>(codexHome), clock);
            shutdown = std::make_unique<WindowsShutdown>();
        }
        Controller controller(*monitor, store, *shutdown, simulation, clock);
        QString startupShortcut = qEnvironmentVariable("APPDATA")
            + "/Microsoft/Windows/Start Menu/Programs/Startup/Codex Shutdown Automation.lnk";
        QFile manifestFile(directory + "/installation.json");
        if (manifestFile.open(QIODevice::ReadOnly)) {
            auto data = manifestFile.readAll();
            if (data.startsWith("\xef\xbb\xbf")) data.remove(0, 3);
            const auto manifest = QJsonDocument::fromJson(data).object();
            const auto recordedPath = manifest["startupShortcutPath"].toString();
            if (QDir::isAbsolutePath(recordedPath)) startupShortcut = recordedPath;
        }
        controller.setStartupEnabled(QFile::exists(startupShortcut));
        MainWindow window(controller);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(serverName);
        if (!server.listen(serverName)) throw std::runtime_error("Cannot start the private local application channel.");
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (server.hasPendingConnections()) {
                QLocalSocket *socket = server.nextPendingConnection();
                auto input = std::make_shared<QByteArray>();
                auto timeout = new QTimer(socket); timeout->setSingleShot(true); timeout->start(2000);
                QObject::connect(timeout, &QTimer::timeout, socket, &QLocalSocket::disconnectFromServer);
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QLocalSocket::readyRead, &app, [&, socket, input, timeout] {
                    *input += socket->readAll();
                    if (input->size() > 4096) { socket->disconnectFromServer(); return; }
                    if (!input->contains('\n')) return;
                    timeout->stop();
                    const auto request = QJsonDocument::fromJson(*input).object();
                    QJsonObject response;
                    bool quit = false;
                    if (request["scope"].toString() != scope) response = {{"error", "Another Codex store is monitored. Quit that instance before changing CODEX_HOME."}};
                    else {
                        const auto command = request["command"].toString();
                        if (command == "show") { window.showNormal(); window.raise(); window.activateWindow(); }
                        else if (command == "quit") { controller.cancel(); quit = true; }
                        else if (command != "status") response = {{"error", "Unsupported local command."}};
                        if (response.isEmpty()) response = compactStatus(controller, scope);
                    }
                    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
                    socket->flush(); socket->disconnectFromServer();
                    if (quit) QTimer::singleShot(100, &app, &QCoreApplication::quit);
                });
            }
        });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&] { controller.cancel(); });
        controller.start();
        if (!parser.isSet("background") || !QSystemTrayIcon::isSystemTrayAvailable()) window.show();
        return app.exec();
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, app.applicationName(), QString::fromUtf8(error.what())); return 1;
    }
}
