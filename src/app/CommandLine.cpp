#include "app/CommandLine.h"

#include "app/PlatformComposition.h"
#include "app/SingleInstanceIpc.h"

#include <QProcess>

#include <iostream>

namespace speecher {
namespace {

std::optional<OutputFormat> requestedOutputFormat(const QStringList &arguments, QString *error)
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

QString requestedOption(const QStringList &arguments, const QString &name, QString *error)
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

bool startDetachedListening(const SingleInstancePlatform *platform, std::optional<OutputFormat> outputFormat)
{
    QStringList arguments{QStringLiteral("--daemon"), QStringLiteral("--start-listening")};
    if (outputFormat) {
        arguments << QStringLiteral("--format") << outputFormatName(*outputFormat);
    }
    return QProcess::startDetached(platform->detachedExecutablePath(), arguments);
}

bool startDetachedSettings(const SingleInstancePlatform *platform)
{
    return QProcess::startDetached(
        platform->detachedExecutablePath(),
        {QStringLiteral("--daemon"), QStringLiteral("--show-settings")});
}

bool startDetachedSetup(const SingleInstancePlatform *platform)
{
    return QProcess::startDetached(
        platform->detachedExecutablePath(),
        {QStringLiteral("--daemon"), QStringLiteral("--show-setup")});
}

} // namespace

CommandLineDecision parseCommandLine(const QStringList &arguments, const QString &logPath)
{
    if (arguments.contains(QStringLiteral("--version"))) {
        std::cout << "speecher " << SPEECHER_VERSION << "\n";
        std::cout << "log " << logPath.toStdString() << "\n";
        return {LaunchMode::Exit};
    }

    CommandLineDecision decision;
    QString optionError;
    decision.grabPath = requestedOption(arguments, QStringLiteral("--grab"), &optionError);
    if (!optionError.isEmpty()) {
        std::cerr << optionError.toStdString() << "\n";
        return {LaunchMode::Exit, 2};
    }

    const QString verb = arguments.size() >= 2 ? arguments.at(1).trimmed().toLower() : QString();
    const bool isCliCommand = verb == QStringLiteral("toggle")
        || verb == QStringLiteral("start")
        || verb == QStringLiteral("stop")
        || verb == QStringLiteral("status")
        || verb == QStringLiteral("settings")
        || verb == QStringLiteral("setup");
    decision.startListening = arguments.contains(QStringLiteral("--start-listening"));
    decision.showSettings = arguments.contains(QStringLiteral("--show-settings"));
    decision.showSetup = arguments.contains(QStringLiteral("--show-setup"));

    QString formatError;
    decision.outputFormat = requestedOutputFormat(arguments, &formatError);
    if (!formatError.isEmpty()) {
        std::cerr << formatError.toStdString() << "\n";
        return {LaunchMode::Exit, 2};
    }

    if (isCliCommand) {
        if (decision.outputFormat && verb != QStringLiteral("toggle") && verb != QStringLiteral("start")) {
            std::cerr << "--format can only be used with toggle or start\n";
            return {LaunchMode::Exit, 2};
        }
        decision.mode = LaunchMode::RunCli;
        decision.ipcCommand = verb == QStringLiteral("settings")
            ? QStringLiteral("showSettings")
            : verb == QStringLiteral("setup")
                ? QStringLiteral("showSetup")
                : verb;
        return decision;
    }

    decision.mode = arguments.contains(QStringLiteral("--daemon"))
        ? LaunchMode::RunDaemon
        : LaunchMode::RunGui;
    return decision;
}

int runCliCommand(const CommandLineDecision &decision,
                  const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    const QString &command = decision.ipcCommand;
    IpcResponse response;
    QString ipcError;
    const IpcCommandResult ipcResult = SingleInstanceIpc::sendCommandDetailed(command,
                                                                              decision.outputFormat,
                                                                              &response,
                                                                              2500,
                                                                              platform,
                                                                              &ipcError);
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
        : command == QStringLiteral("showSetup")
            ? startDetachedSetup(platform.get())
            : startDetachedListening(platform.get(), decision.outputFormat);
    if (!started) {
        std::cerr << "Could not start speecher daemon\n";
        return 1;
    }
    return 0;
}

} // namespace speecher
