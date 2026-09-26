#include "app/CommandLine.h"

#include "app/PlatformComposition.h"
#include "app/ProviderSetup.h"
#include "app/SingleInstanceIpc.h"
#include "providers/ProviderRegistry.h"
#include "transcribe/FileTranscriptionSession.h"

#include <QFileInfo>
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

QStringList absolutePaths(const QStringList &paths)
{
    QStringList absolute;
    for (const QString &path : paths) {
        absolute << QFileInfo(path).absoluteFilePath();
    }
    return absolute;
}

const char kHelp[] = R"(Usage: speecher [command] [options]

Commands (sent to the running Speecher):
  toggle | start | stop    control dictation
  status                   print the dictation state
  settings | setup         open settings or the setup assistant
  quit                     quit the running Speecher

  transcribe <files...>    open the files in the Transcribe window
  <audio files...>         the same, as a file manager's "Open with" does

Transcribe without a window, printing the results:
  speecher transcribe [options] <files...>
  --headless               transcribe with the settings' choices; any option
                           below also implies it
  --model <id>             speech provider: %1
  --no-vocabulary          skip the custom vocabulary and corrections
  --refine <id|none>       refinement provider: %2, none
  --cleanup <level>        %3
  --profile <name>         writing profile; seeds cleanup and tone: %4
  --tone <name>            %5
  --output <beside|none|DIR>
                           where to save <name>-transcribed.txt (default beside)
  --stdout                 also print each transcript
  --raw                    print and save the raw transcript, not the refined one
  --json                   print one JSON object per file, then a summary
  Exit status: 0 all files transcribed, 1 some failed, 2 usage error.

Options:
  --format plain|html      output format for toggle and start
  --daemon                 run without a window
  --version                print the version
  --help                   print this help
)";

// Each stored id and the name the command line gives it. Spelled out rather
// than derived from the settings labels, so renaming a label never changes the
// command line.
using CliNames = QList<std::pair<QString, QString>>;

const CliNames kCleanupNames{{QStringLiteral("none"), QStringLiteral("none")},
                             {QStringLiteral("light_cleanup"), QStringLiteral("light")},
                             {QStringLiteral("balanced"), QStringLiteral("medium")},
                             {QStringLiteral("strong_polish"), QStringLiteral("high")}};
const CliNames kProfileNames{{QStringLiteral("work"), QStringLiteral("work")},
                             {QStringLiteral("email"), QStringLiteral("email")},
                             {QStringLiteral("personal"), QStringLiteral("personal")},
                             {QStringLiteral("ai_coding"), QStringLiteral("ai-coding")},
                             {QStringLiteral("other"), QStringLiteral("other")}};
const CliNames kToneNames{{QStringLiteral("none"), QStringLiteral("none")},
                          {QStringLiteral("formal"), QStringLiteral("formal")},
                          {QStringLiteral("casual"), QStringLiteral("casual")},
                          {QStringLiteral("very_casual"), QStringLiteral("very-casual")},
                          {QStringLiteral("excited"), QStringLiteral("excited")},
                          {QStringLiteral("gen_z"), QStringLiteral("gen-z")}};

QStringList cliNames(const CliNames &choices)
{
    QStringList names;
    for (const auto &[id, name] : choices) {
        names << name;
    }
    return names;
}

QStringList providerIds(const QList<ProviderDescriptor> &providers)
{
    QStringList ids;
    for (const ProviderDescriptor &provider : providers) {
        ids << provider.id;
    }
    ids.sort();
    return ids;
}

// Lists the providers from the registry the app builds; registering creates
// no provider, so this is cheap and needs no credentials.
QString helpText()
{
    ProviderRegistry registry;
    registerProviders(registry, nullptr);
    const QString separator = QStringLiteral(", ");
    return QString::fromUtf8(kHelp)
        .arg(providerIds(registry.speechProviders()).join(separator),
             providerIds(registry.refinementProviders()).join(separator),
             cliNames(kCleanupNames).join(separator),
             cliNames(kProfileNames).join(separator),
             cliNames(kToneNames).join(separator));
}

// The stored id for a command-line name, or nothing for a name not offered.
std::optional<QString> storedId(const CliNames &choices, const QString &name)
{
    const qsizetype index = cliNames(choices).indexOf(name.toLower());
    return index < 0 ? std::nullopt : std::optional(choices.at(index).first);
}

// Reads `speecher transcribe`'s arguments. Returns an error message for a
// usage mistake.
QString parseTranscribeArguments(const QStringList &arguments, CommandLineDecision *decision)
{
    HeadlessTranscribeOptions &options = decision->headless;
    bool headless = false;
    QStringList files;
    bool optionsEnded = false;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (optionsEnded || !argument.startsWith(QLatin1Char('-'))) {
            files << argument;
            continue;
        }
        if (argument == QStringLiteral("--")) {
            optionsEnded = true;
            continue;
        }
        // Read before this for the screenshot rig, which opens the window.
        if (argument == QStringLiteral("--grab")) {
            ++index;
            continue;
        }
        if (argument.startsWith(QStringLiteral("--grab="))) {
            continue;
        }
        headless = true;
        const auto value = [&]() -> std::optional<QString> {
            if (index + 1 < arguments.size()) {
                return arguments.at(++index);
            }
            return std::nullopt;
        };
        const auto choice = [&](const CliNames &choices, std::optional<QString> *target) -> QString {
            const std::optional<QString> given = value();
            if (!given) {
                return QStringLiteral("%1 requires a value").arg(argument);
            }
            *target = storedId(choices, *given);
            return *target ? QString()
                           : QStringLiteral("Unknown %1 value: %2 (expected %3)")
                                 .arg(argument, *given, cliNames(choices).join(QStringLiteral(", ")));
        };
        QString error;
        if (argument == QStringLiteral("--headless")) {
        } else if (argument == QStringLiteral("--no-vocabulary")) {
            options.applyVocabulary = false;
        } else if (argument == QStringLiteral("--stdout")) {
            options.printTranscripts = true;
        } else if (argument == QStringLiteral("--raw")) {
            options.raw = true;
        } else if (argument == QStringLiteral("--json")) {
            options.json = true;
        } else if (argument == QStringLiteral("--model") || argument == QStringLiteral("--refine")) {
            // Checked against the registry once it exists; this only reads it.
            const std::optional<QString> given = value();
            if (!given) {
                error = QStringLiteral("%1 requires a value").arg(argument);
            } else {
                (argument == QStringLiteral("--model") ? options.speechProviderId
                                                       : options.refinementProviderId) = given->toLower();
            }
        } else if (argument == QStringLiteral("--cleanup")) {
            error = choice(kCleanupNames, &options.cleanupStrength);
        } else if (argument == QStringLiteral("--profile")) {
            error = choice(kProfileNames, &options.writingProfile);
        } else if (argument == QStringLiteral("--tone")) {
            error = choice(kToneNames, &options.tone);
        } else if (argument == QStringLiteral("--output")) {
            const std::optional<QString> given = value();
            if (!given) {
                error = QStringLiteral("--output requires beside, none or a folder");
            } else if (given->toLower() == QStringLiteral("beside")) {
                options.destination = TranscriptDestination::BesideInput;
            } else if (given->toLower() == QStringLiteral("none")) {
                options.destination = TranscriptDestination::None;
            } else if (QFileInfo(*given).isDir()) {
                options.destination = TranscriptDestination::Folder;
                options.folder = QFileInfo(*given).absoluteFilePath();
            } else {
                error = QStringLiteral("--output folder does not exist: %1").arg(*given);
            }
        } else if (argument == QStringLiteral("--daemon")) {
            error = QStringLiteral("--daemon cannot be used with transcribe");
        } else {
            error = QStringLiteral("Unknown transcribe option: %1").arg(argument);
        }
        if (!error.isEmpty()) {
            return error;
        }
    }
    if (files.isEmpty()) {
        return QStringLiteral("transcribe needs at least one audio file");
    }
    decision->transcribeFiles = absolutePaths(files);
    if (!headless) {
        return {};
    }
    for (const QString &path : std::as_const(decision->transcribeFiles)) {
        if (!QFileInfo(path).isReadable() || !QFileInfo(path).isFile()) {
            return QStringLiteral("Cannot read %1").arg(path);
        }
    }
    decision->mode = LaunchMode::TranscribeHeadless;
    return {};
}

} // namespace

CommandLineDecision parseCommandLine(const QStringList &arguments, const QString &logPath)
{
    if (arguments.contains(QStringLiteral("--version"))) {
        std::cout << "speecher " << SPEECHER_VERSION << " (build " << SPEECHER_BUILD_NUMBER << ")\n";
        std::cout << "log " << logPath.toStdString() << "\n";
        return {LaunchMode::Exit};
    }

    if (arguments.contains(QStringLiteral("--help")) || arguments.contains(QStringLiteral("-h"))) {
        std::cout << helpText().toStdString();
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
        || verb == QStringLiteral("setup")
        || verb == QStringLiteral("grab")
        || verb == QStringLiteral("quit");
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
    if (verb == QStringLiteral("transcribe")) {
        const QString error = parseTranscribeArguments(arguments.mid(2), &decision);
        if (!error.isEmpty()) {
            std::cerr << error.toStdString() << "\n\n"
                      << helpText().toStdString();
            return {LaunchMode::Exit, 2};
        }
        if (decision.mode == LaunchMode::TranscribeHeadless) {
            return decision;
        }
    } else {
        QStringList files;
        for (const QString &argument : arguments.mid(1)) {
            if (isAudioFile(argument)) {
                files << argument;
            }
        }
        decision.transcribeFiles = absolutePaths(files);
    }
    if (!decision.transcribeFiles.isEmpty()) {
        decision.mode = LaunchMode::RunGui;
    }
    return decision;
}

QStringList argumentsWithoutStartupActions(const QStringList &arguments)
{
    static const QStringList startupActions{QStringLiteral("--start-listening"),
                                            QStringLiteral("--show-settings"),
                                            QStringLiteral("--show-setup")};
    QStringList kept;
    kept.reserve(arguments.size());
    for (const QString &argument : arguments) {
        if (!startupActions.contains(argument)) {
            kept << argument;
        }
    }
    return kept;
}

bool quitOnLastWindowClosed(LaunchMode mode)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    Q_UNUSED(mode)
    return false;
#else
    return mode != LaunchMode::RunDaemon;
#endif
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

    if (command == QStringLiteral("stop") || command == QStringLiteral("status")
        || command == QStringLiteral("quit") || command == QStringLiteral("grab")) {
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
