#include "app/ApplicationController.h"
#include "app/UpdateController.h"
#include "app/CommandLine.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/qt/QtFrontEnd.h"
#include "ui/Theme.h"

#ifdef SPEECHER_WITH_SWIFT_UI
#include "frontend/mac/MacFrontEnd.h"
#endif

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QMutex>

#include <iostream>

using namespace speecher;

static QFile *g_logFile = nullptr;
static QMutex g_logMutex;

static void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    QMutexLocker locker(&g_logMutex);
    if (!g_logFile || !g_logFile->isOpen()) {
        return;
    }
    const char *level = "info";
    if (type == QtDebugMsg) {
        level = "debug";
    } else if (type == QtWarningMsg) {
        level = "warning";
    } else if (type == QtCriticalMsg) {
        level = "critical";
    } else if (type == QtFatalMsg) {
        level = "fatal";
    }
    QTextStream stream(g_logFile);
    stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << ' ' << level << ' ' << message << '\n';
    stream.flush();
}

static QString installLogHandler()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.cache/speecher");
    }
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/speecher.log");
    g_logFile = new QFile(path);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
    }
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "speecher log started path=" + path;
    return path;
}

static void migrateSettings()
{
    constexpr auto oldOrganization = "local.speecher";
    QSettings newSettings(QString::fromLatin1(SettingsKeys::Organization),
                          QString::fromLatin1(SettingsKeys::Application));
    QSettings oldSettings(QString::fromLatin1(oldOrganization),
                          QString::fromLatin1(SettingsKeys::Application));
    QString error;
    if (!migrateSettingsIdentity(newSettings, oldSettings, &error)) {
        qWarning().noquote() << error;
    }
}

static QStringList commandLineArguments(int argc, char **argv)
{
    QStringList arguments;
    arguments.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        arguments << QString::fromLocal8Bit(argv[index]);
    }
    return arguments;
}

int main(int argc, char **argv)
{
    QCoreApplication::setApplicationName(QStringLiteral("speecher"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.firemonster612.speecher"));
    QCoreApplication::setOrganizationName(QString::fromLatin1(SettingsKeys::Organization));
    const QString logPath = installLogHandler();
    migrateSettings();
    const std::shared_ptr<const PlatformComposition> platform = platformComposition();

    const CommandLineDecision decision =
        parseCommandLine(commandLineArguments(argc, argv), logPath);
    if (decision.mode == LaunchMode::Exit) {
        return decision.exitCode;
    }
    if (decision.mode == LaunchMode::RunCli) {
        // No QApplication: talking to a running instance must not need a display.
        QCoreApplication app(argc, argv);
        return runCliCommand(decision, platform);
    }

    QApplication app(argc, argv);
    // A second store, because the theme has to be applied before the first
    // widget exists and the controller's store is not built yet.
    SettingsStore startupSettings;
    Theme::apply(startupSettings.theme());

    const bool daemon = decision.mode == LaunchMode::RunDaemon;
    app.setQuitOnLastWindowClosed(quitOnLastWindowClosed(decision.mode));

    ApplicationController controller(daemon, platform);
    // The one place a platform's front end is chosen; see
    // docs/adr/0001-per-platform-front-ends.md.
#ifdef SPEECHER_WITH_SWIFT_UI
    MacFrontEnd frontEnd(&controller);
#else
    QtFrontEnd frontEnd(&controller);
#endif
    controller.setFrontEnd(&frontEnd);
    controller.updates()->start();
    QString ipcError;
    if (!controller.startIpc(&ipcError)) {
        if (!decision.grabPath.isEmpty()) {
            if (ipcError.startsWith(QStringLiteral("Another Speecher instance"))) {
                std::cerr << "--grab cannot be used while another Speecher instance is running\n";
            } else {
                std::cerr << ipcError.toStdString() << "\n";
            }
            return 1;
        }
        const QString showCommand = decision.showSettings ? QStringLiteral("showSettings")
                                                          : QStringLiteral("showMain");
        if (!daemon && SingleInstanceIpc::sendCommand(showCommand, nullptr)) {
            return 0;
        }
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if ((!controller.settings()->setupCompleted() && decision.grabPath.isEmpty()) || decision.showSetup) {
        QTimer::singleShot(0, &controller, [&controller] {
            controller.showSetupAssistant();
        });
    } else {
        if (decision.startListening) {
            QTimer::singleShot(0, &controller, [&controller, &decision] {
                if (decision.outputFormat) {
                    controller.handleIpcCommand(QStringLiteral("start"),
                                                outputFormatName(*decision.outputFormat),
                                                nullptr);
                } else {
                    controller.startListening();
                }
            });
        }
        if (decision.showSettings) {
            QTimer::singleShot(0, &controller, &ApplicationController::showSettings);
        }
        if (!daemon || !decision.grabPath.isEmpty()) {
            controller.showMainWindow();
        }
    }
    if (!decision.grabPath.isEmpty()) {
        QTimer::singleShot(600, &controller, [&controller, &app, &decision] {
            app.exit(controller.grabMainWindow(decision.grabPath) ? 0 : 1);
        });
    }
    return app.exec();
}
