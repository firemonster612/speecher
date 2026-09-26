#include "app/ApplicationController.h"
#ifdef Q_OS_LINUX
#include "app/AppImageUpdater.h"
#endif
#include "app/UpdateController.h"
#include "app/CommandLine.h"
#include "app/PlatformComposition.h"
#include "app/ProviderSetup.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "providers/ProviderRegistry.h"
#ifndef SPEECHER_WITH_WINUI
#include "frontend/qt/QtFrontEnd.h"
#include "ui/Theme.h"
#endif

#ifdef Q_OS_LINUX
#include "ui/HostStylePlugin.h"
#include "platform/LinuxStyleChoice.h"
#endif

#ifdef SPEECHER_WITH_SWIFT_UI
#include "frontend/mac/MacFrontEnd.h"
#endif
#ifdef SPEECHER_WITH_WINUI
#include "frontend/win/WinFrontEnd.h"
#include "frontend/win/WinUiHost.h"
#include <windows.h>
#endif

#include <QApplication>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <QDeadlineTimer>
#include <QThread>
#endif
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#ifdef Q_OS_LINUX
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#endif
#include <QTextStream>
#include <QTimer>
#include <QMutex>

#include <cstdio>
#include <iostream>
#ifdef Q_OS_WIN
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

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
    migrateRefinementModels(newSettings);
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

#ifdef Q_OS_LINUX
static QString kdeWidgetStyle()
{
    const QStringList paths = QStandardPaths::locateAll(
        QStandardPaths::GenericConfigLocation,
        QStringLiteral("kdeglobals"));
    for (const QString &path : paths) {
        QSettings kdeglobals(path, QSettings::IniFormat);
        if (kdeglobals.contains(QStringLiteral("KDE/widgetStyle"))) {
            return kdeglobals.value(QStringLiteral("KDE/widgetStyle")).toString();
        }
    }
    return {};
}

static void applyHostWidgetStyle(const QString &applicationTheme)
{
    const QString currentStyle = qApp->style()->objectName();
    const LinuxStyleChoice choice = chooseLinuxStyle(
        qEnvironmentVariable("QT_STYLE_OVERRIDE"),
        kdeWidgetStyle(),
        qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        qEnvironmentVariable("QT_QPA_PLATFORMTHEME"),
        currentStyle,
        QStyleFactory::keys(),
        applicationTheme,
        qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    // The desktop asked for a style this Qt does not carry: try the plugin
    // installed on the system before settling for the fallback. Qt refuses a
    // plugin built for a newer Qt than the one running, so this is best effort.
    if (!choice.requested.isEmpty()
        && choice.chosen.compare(choice.requested, Qt::CaseInsensitive) != 0) {
        QStringList attempts;
        if (QStyle *host = loadHostStyle(choice.requested, hostStylePluginDirs(), &attempts)) {
            QApplication::setStyle(host);
            qInfo().noquote() << "widget style requested=" + choice.requested
                                    + " chosen=" + qApp->style()->objectName() + " (system plugin)";
            return;
        }
        for (const QString &attempt : attempts) {
            qInfo().noquote() << "system style plugin " + attempt;
        }
    }
    if (currentStyle.compare(choice.chosen, Qt::CaseInsensitive) != 0
        && !QApplication::setStyle(choice.chosen)) {
        qWarning().noquote() << "Could not load widget style " + choice.chosen;
        if (currentStyle.compare(choice.fallback, Qt::CaseInsensitive) != 0
            && choice.chosen.compare(choice.fallback, Qt::CaseInsensitive) != 0
            && !QApplication::setStyle(choice.fallback)) {
            qWarning().noquote() << "Could not load fallback widget style " + choice.fallback;
        }
    }
    const QString requested = choice.requested.isEmpty() ? QStringLiteral("<none>") : choice.requested;
    qInfo().noquote() << "widget style requested=" + requested
                            + " chosen=" + qApp->style()->objectName();
}
#endif

int main(int argc, char **argv)
{
    QCoreApplication::setApplicationName(QStringLiteral("speecher"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.firemonster612.speecher"));
    QCoreApplication::setOrganizationName(QString::fromLatin1(SettingsKeys::Organization));
    const QString logPath = installLogHandler();
    migrateSettings();
    const std::shared_ptr<const PlatformComposition> platform = platformComposition();

    const QStringList arguments = commandLineArguments(argc, argv);
    const CommandLineDecision decision = parseCommandLine(arguments, logPath);
    if (decision.mode == LaunchMode::Exit) {
        return decision.exitCode;
    }
    if (decision.mode == LaunchMode::RunCli) {
        // No QApplication: talking to a running instance must not need a display.
        QCoreApplication app(argc, argv);
        return runCliCommand(decision, platform);
    }
    if (decision.mode == LaunchMode::TranscribeHeadless) {
        // Its own settings reader, secrets and providers: no IPC, no display,
        // and nothing shared with a running instance's dictation.
        QCoreApplication app(argc, argv);
        SettingsStore settings;
        SecretStore secrets(&settings);
        ProviderRegistry providers;
        registerProviders(providers, &secrets);
        return runHeadlessTranscribe(decision.transcribeFiles, decision.headless, &settings, &providers,
                                     std::cout, std::cerr, isatty(fileno(stderr)));
    }

#ifdef SPEECHER_WITH_WINUI
    auto winUiHost = std::make_unique<WinUiHost>();
#endif
    QApplication app(argc, argv);
#ifdef SPEECHER_WITH_WINUI
    winUiHost->installNativeEventFilter();
#endif
    // A second store, because the theme has to be applied before the first
    // widget exists and the controller's store is not built yet.
    SettingsStore startupSettings;
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        applyHostWidgetStyle(startupSettings.theme());
    }
#endif
#ifdef Q_OS_LINUX
    AppImageUpdater::waitForRestartParent();
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }
#endif
#ifndef SPEECHER_WITH_WINUI
    Theme::apply(startupSettings.theme());
#endif

    const bool daemon = decision.mode == LaunchMode::RunDaemon;
    app.setQuitOnLastWindowClosed(quitOnLastWindowClosed(decision.mode));

    ApplicationController controller(daemon, platform);
    QObject::connect(&controller,
                     &ApplicationController::quitRequested,
                     &app,
                     &QCoreApplication::quit,
                     Qt::QueuedConnection);
    // The one place a platform's front end is chosen; see
    // docs/adr/0001-per-platform-front-ends.md.
#ifdef SPEECHER_WITH_WINUI
    WinFrontEnd frontEnd(&controller, std::move(winUiHost));
#elif defined(SPEECHER_WITH_SWIFT_UI)
    MacFrontEnd frontEnd(&controller);
#else
    QtFrontEnd frontEnd(&controller);
#endif
    controller.setFrontEnd(&frontEnd);
#ifdef SPEECHER_WITH_WINUI
    if (arguments.contains(QStringLiteral("--winui-spike"))) {
        QTimer::singleShot(0, &controller, [&frontEnd] {
            frontEnd.showDictationError(QStringLiteral("WinUI hosting spike"));
        });
    }
#endif
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
        const QString showCommand = !decision.transcribeFiles.isEmpty()
            ? QStringLiteral("transcribe")
            : decision.showSettings ? QStringLiteral("showSettings")
                                    : QStringLiteral("showMain");
        // An older running instance answers a command it does not know, such
        // as transcribe, with a refusal; say so rather than exit as if the
        // files had opened.
        IpcResponse response;
        const auto answered = [&response, &showCommand] {
            if (response.ok) {
                return 0;
            }
            std::cerr << "The running Speecher instance refused " << showCommand.toStdString() << ": "
                      << response.message.toStdString() << "\n";
            return 1;
        };
#ifdef Q_OS_WIN
        if (!daemon) {
            AllowSetForegroundWindow(ASFW_ANY);
            auto result = SingleInstanceIpc::sendCommandDetailed(
                showCommand, std::nullopt, decision.transcribeFiles, &response);
            // The startup claim can precede the winning instance's pipe listener.
            if (ipcError.startsWith(QStringLiteral("Another Speecher instance"))) {
                QDeadlineTimer deadline(750);
                while (result == IpcCommandResult::Unavailable && !deadline.hasExpired()) {
                    QThread::msleep(25);
                    const auto remaining = deadline.remainingTime();
                    if (remaining <= 0) {
                        break;
                    }
                    result = SingleInstanceIpc::sendCommandDetailed(
                        showCommand, std::nullopt, decision.transcribeFiles, &response, int(remaining));
                }
            }
            if (result == IpcCommandResult::Sent) {
                return answered();
            }
        }
#else
        if (!daemon
            && SingleInstanceIpc::sendCommandDetailed(showCommand, std::nullopt, decision.transcribeFiles, &response)
                == IpcCommandResult::Sent) {
            return answered();
        }
#endif
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if ((!controller.settings()->setupCompleted() && decision.grabPath.isEmpty()) || decision.showSetup) {
        QTimer::singleShot(0, &controller, [&controller, &decision] {
            controller.showSetupAssistant();
            // Held by the controller until setup completes, then opened.
            if (!decision.transcribeFiles.isEmpty()) {
                controller.showTranscribeFiles(decision.transcribeFiles);
            }
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
        // An update restart records which windows were on screen; the relaunched
        // process puts them back. Never the microphone: a new process has no
        // dictation gesture behind it, so nothing here starts listening.
        // Read-and-clear so a normal launch never replays it, and only honor a
        // token young enough to be from an actual relaunch.
        const QString restore = controller.settings()->updatesRestoreState();
        const qint64 restoreAge = QDateTime::currentMSecsSinceEpoch()
            - controller.settings()->updatesRestoreStateTime();
        controller.settings()->setUpdatesRestoreState({});
        if (!restore.isEmpty() && restoreAge >= 0 && restoreAge < 120000
            && restore.contains(QStringLiteral("settings"))) {
            QTimer::singleShot(0, &controller, &ApplicationController::showSettings);
        }
        if (!decision.grabPath.isEmpty()) {
            controller.showMainWindow();
        } else if (!daemon && decision.transcribeFiles.isEmpty()) {
            // A launch that opens files brings up the Transcribe window alone.
            // macOS hands Finder's files over as events once the loop runs,
            // so let those arrive before deciding.
            QTimer::singleShot(0, &controller, [&controller] {
                QCoreApplication::processEvents();
                if (!controller.filesOpened()) {
                    controller.showMainWindow();
                }
            });
        }
        if (!decision.transcribeFiles.isEmpty()) {
            QTimer::singleShot(0, &controller, [&controller, &decision] {
                controller.showTranscribeFiles(decision.transcribeFiles);
            });
        }
    }
    if (!decision.grabPath.isEmpty()) {
        QTimer::singleShot(600, &controller, [&controller, &app, &decision] {
            app.exit(controller.grabMainWindow(decision.grabPath) ? 0 : 1);
        });
    }
    return app.exec();
}
