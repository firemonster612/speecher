#include "app/ApplicationController.h"
#include "app/LinuxComposition.h"
#include "app/SingleInstanceIpc.h"
#include "core/SettingsStore.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
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
    g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "speecher log started path=" + path;
    return path;
}

static std::optional<OutputFormat> requestedOutputFormat(const QStringList &arguments, QString *error)
{
    const qsizetype optionIndex = arguments.indexOf(QStringLiteral("--format"));
    if (optionIndex < 0) {
        return std::nullopt;
    }
    if (optionIndex + 1 >= arguments.size()) {
        if (error) {
            *error = QStringLiteral("--format requires plain or html");
        }
        return std::nullopt;
    }
    const QString value = arguments.at(optionIndex + 1).trimmed().toLower();
    if (value != QStringLiteral("plain") && value != QStringLiteral("html")) {
        if (error) {
            *error = QStringLiteral("Unknown output format: %1").arg(value);
        }
        return std::nullopt;
    }
    return outputFormatFromString(value);
}

static QString requestedOption(const QStringList &arguments, const QString &name, QString *error)
{
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument.startsWith(name + QStringLiteral("="))) {
            const QString value = argument.mid(name.size() + 1);
            if (!value.isEmpty()) {
                return value;
            }
        } else if (argument == name) {
            if (index + 1 < arguments.size() && !arguments.at(index + 1).startsWith(QLatin1Char('-'))) {
                return arguments.at(index + 1);
            }
        } else {
            continue;
        }
        if (error) {
            *error = QStringLiteral("%1 requires a value").arg(name);
        }
        return {};
    }
    return {};
}

static bool startDetachedListening(const SingleInstancePlatform *platform, std::optional<OutputFormat> outputFormat)
{
    QStringList arguments{QStringLiteral("--daemon"), QStringLiteral("--start-listening")};
    if (outputFormat) {
        arguments << QStringLiteral("--format") << outputFormatName(*outputFormat);
    }
    return QProcess::startDetached(platform->detachedExecutablePath(), arguments);
}

static bool startDetachedSettings(const SingleInstancePlatform *platform)
{
    return QProcess::startDetached(
        platform->detachedExecutablePath(),
        {QStringLiteral("--daemon"), QStringLiteral("--show-settings")});
}

static int runCliCommand(const QString &command,
                         std::optional<OutputFormat> outputFormat,
                         const QString &uiPrototype,
                         const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    IpcResponse response;
    QString ipcError;
    const IpcCommandResult ipcResult = SingleInstanceIpc::sendCommandDetailed(command,
                                                                              outputFormat,
                                                                              &response,
                                                                              1200,
                                                                              platform,
                                                                              &ipcError,
                                                                              uiPrototype);
    if (ipcResult == IpcCommandResult::Sent) {
        std::cout << response.state.toStdString() << "\n";
        return response.ok ? 0 : 1;
    }
    if (ipcResult != IpcCommandResult::Unavailable) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if (command == QStringLiteral("stop") || command == QStringLiteral("status")) {
        std::cout << "idle\n";
        return 0;
    }
    const bool started = command == QStringLiteral("showSettings")
        ? startDetachedSettings(platform.get())
        : startDetachedListening(platform.get(), outputFormat);
    if (!started) {
        std::cerr << "Could not start speecher daemon\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("speecher"));
    QApplication::setDesktopFileName(QStringLiteral("local.speecher"));
    QApplication::setOrganizationName(QStringLiteral("local.speecher"));
    SettingsStore startupSettings;
    Theme::apply(startupSettings.theme());
    const QString logPath = installLogHandler();
    const std::shared_ptr<const LinuxComposition> platform = linuxComposition();

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--version"))) {
        std::cout << "speecher " << SPEECHER_VERSION << "\n";
        std::cout << "log " << logPath.toStdString() << "\n";
        return 0;
    }

    QString optionError;
    const QString requestedPrototype = requestedOption(args, QStringLiteral("--ui"), &optionError).toLower();
    if (!optionError.isEmpty()) {
        std::cerr << optionError.toStdString() << "\n";
        return 2;
    }
    if (!requestedPrototype.isEmpty()) {
        if (requestedPrototype != QStringLiteral("legacy")
            && requestedPrototype != QStringLiteral("a")
            && requestedPrototype != QStringLiteral("b")
            && requestedPrototype != QStringLiteral("c")) {
            std::cerr << "Unknown UI prototype: " << requestedPrototype.toStdString() << "\n";
            return 2;
        }
        startupSettings.setUiPrototype(requestedPrototype);
    }
    const QString grabPath = requestedOption(args, QStringLiteral("--grab"), &optionError);
    if (!optionError.isEmpty()) {
        std::cerr << optionError.toStdString() << "\n";
        return 2;
    }

    const QString cliCommand = args.size() >= 2 ? args.at(1).trimmed().toLower() : QString();
    const bool isCliCommand = cliCommand == QStringLiteral("toggle")
        || cliCommand == QStringLiteral("start")
        || cliCommand == QStringLiteral("stop")
        || cliCommand == QStringLiteral("status")
        || cliCommand == QStringLiteral("settings");
    const bool daemon = args.contains(QStringLiteral("--daemon"));
    const bool startListening = args.contains(QStringLiteral("--start-listening"));
    const bool showSettings = args.contains(QStringLiteral("--show-settings"));
    app.setQuitOnLastWindowClosed(!daemon);
    QString formatError;
    const std::optional<OutputFormat> outputFormat = requestedOutputFormat(args, &formatError);
    if (!formatError.isEmpty()) {
        std::cerr << formatError.toStdString() << "\n";
        return 2;
    }

    if (isCliCommand) {
        if (outputFormat && cliCommand != QStringLiteral("toggle") && cliCommand != QStringLiteral("start")) {
            std::cerr << "--format can only be used with toggle or start\n";
            return 2;
        }
        return runCliCommand(cliCommand == QStringLiteral("settings")
                                 ? QStringLiteral("showSettings")
                                 : cliCommand,
                             outputFormat,
                             requestedPrototype,
                             platform);
    }

    ApplicationController controller(daemon);
    QString ipcError;
    if (!controller.startIpc(&ipcError)) {
        if (!grabPath.isEmpty()) {
            if (ipcError.startsWith(QStringLiteral("Another Speecher instance"))) {
                std::cerr << "--grab cannot be used while another Speecher instance is running\n";
            } else {
                std::cerr << ipcError.toStdString() << "\n";
            }
            return 1;
        }
        const QString showCommand = showSettings ? QStringLiteral("showSettings")
                                                 : QStringLiteral("showMain");
        if (!daemon && SingleInstanceIpc::sendCommand(showCommand,
                                                       nullptr,
                                                       1200,
                                                       {},
                                                       requestedPrototype)) {
            return 0;
        }
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if (startListening) {
        QTimer::singleShot(0, &controller, [&controller, outputFormat] {
            if (outputFormat) {
                controller.handleIpcCommand(QStringLiteral("start"),
                                            outputFormatName(*outputFormat),
                                            {},
                                            nullptr);
            } else {
                controller.startListening();
            }
        });
    }
    if (showSettings) {
        QTimer::singleShot(0, &controller, &ApplicationController::showSettings);
    }
    if (!daemon || !grabPath.isEmpty()) {
        controller.showMainWindow();
    }
    if (!grabPath.isEmpty()) {
        QTimer::singleShot(600, &controller, [&controller, &app, grabPath] {
            app.exit(controller.grabMainWindow(grabPath) ? 0 : 1);
        });
    }
    return app.exec();
}
