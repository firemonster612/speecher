#include "app/ApplicationController.h"
#include "app/AppImageUpdater.h"
#include "app/UpdateController.h"
#include "app/CommandLine.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"
#include "frontend/qt/QtFrontEnd.h"
#include "ui/Theme.h"

#ifdef Q_OS_LINUX
#include "platform/LinuxStyleChoice.h"
#endif

#ifdef SPEECHER_WITH_SWIFT_UI
#include "frontend/mac/MacFrontEnd.h"
#endif

#include <QApplication>
#include <QDateTime>
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
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        applyHostWidgetStyle(QStringLiteral("system"));
    }
#endif
    AppImageUpdater::waitForRestartParent();
#ifdef Q_OS_LINUX
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }
#endif
    Theme::apply(startupSettings.theme());

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
        const QString showCommand = decision.showSettings
            ? showSettingsCommand(decision.settingsPage)
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
            QTimer::singleShot(0, &controller, [&controller, &decision] {
                controller.showSettings(decision.settingsPage);
            });
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
