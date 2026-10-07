#include "app/CommandLine.h"

#include "app/PlatformComposition.h"
#include "app/ProviderSetup.h"
#include "app/SingleInstanceIpc.h"
#include "core/settings/SettingsCodecs.h"
#include "core/settings/SettingsSchema.h"
#include "core/settings/SpokenLanguages.h"
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

bool startDetachedListening(const SingleInstancePlatform *platform, const QStringList &overrideArguments)
{
    return QProcess::startDetached(platform->detachedExecutablePath(),
                                   QStringList{QStringLiteral("--daemon"), QStringLiteral("--start-listening")}
                                       + overrideArguments);
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

// How long `listen --until-silence` waits without speech when given no
// seconds, and the least and most it takes.
constexpr double kDefaultUntilSilenceSeconds = 2;
constexpr double kMinUntilSilenceSeconds = 0.1;
constexpr int kMaxUntilSilenceSeconds = 3600;

const char kHelp[] = R"(Usage: speecher [command] [options]

Commands (sent to the running Speecher):
  toggle | start | stop    control dictation
  cancel                   throw away the dictation in progress
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
  --language <code>        spoken language: a code such as de, or auto
  --output <beside|none|DIR>
                           where to save <name>-transcribed.txt (default beside)
  --stdout                 also print each transcript
  --raw                    print and save the raw transcript, not the refined one
  --json                   print one JSON object per file, then a summary
  Exit status: 0 all files transcribed, 1 some failed, 2 usage error.

Record from the microphone once and print what was said:
  speecher listen [options]
  --until-silence [seconds]
                           also stop after this much silence once speech has
                           started (default 2); Ctrl-C, and Enter at a
                           terminal, always stop, keeping what was said
  Takes --model, --no-vocabulary, --refine, --cleanup, --profile, --tone,
  --language, --raw and --json as transcribe does; --json prints one object.
  Exit status: 0 transcript printed, 1 failed or heard no speech, 2 usage
  error.

Options:
  --format plain|html      output format for toggle and start
  --profile <name>         writing profile for toggle and start: %4
  --language <code>        spoken language for toggle and start: a code such
                           as de, or auto
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
const CliNames kToneNames{{QStringLiteral("none"), QStringLiteral("none")},
                          {QStringLiteral("formal"), QStringLiteral("formal")},
                          {QStringLiteral("casual"), QStringLiteral("casual")},
                          {QStringLiteral("very_casual"), QStringLiteral("very-casual")},
                          {QStringLiteral("excited"), QStringLiteral("excited")},
                          {QStringLiteral("gen_z"), QStringLiteral("gen-z")}};

// The built-in names, then each custom tone or level the settings hold, by
// its id without custom_ and with - for _.
CliNames withCustomNames(CliNames names, const QList<RowOption> &options)
{
    for (const RowOption &option : options) {
        if (option.id.startsWith(kCustomIdPrefix)) {
            names.append({option.id,
                          option.id.mid(kCustomIdPrefix.size()).replace(QLatin1Char('_'), QLatin1Char('-'))});
        }
    }
    return names;
}

CliNames cleanupNames()
{
    return withCustomNames(kCleanupNames, cleanupStrengths(SettingsCodecs().customCleanupLevels()));
}

CliNames toneNames()
{
    return withCustomNames(kToneNames, writingTones(SettingsCodecs().customTones()));
}

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

QList<RowOption> writingProfiles()
{
    return writingProfileChoices(SettingsCodecs().writingProfileSettings());
}

// The profiles' current names, for --help and errors. A name with a space is
// quoted, as a shell needs it.
QString writingProfileNames(const QList<RowOption> &profiles)
{
    QStringList names;
    for (const RowOption &profile : profiles) {
        names << (profile.label.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(profile.label)
                                                           : profile.label);
    }
    return names.join(QStringLiteral(", "));
}

// The id of the Writing Profile a command-line value names: a profile's
// current name in any case, or that name with - between words. Failing that,
// an exact id, which is how a command hands the profile to the daemon it
// starts. Sets error when the value names no profile or more than one.
std::optional<QString> writingProfileNamed(const QString &value, QString *error)
{
    const QList<RowOption> profiles = writingProfiles();
    const QString wanted = value.trimmed().toLower();
    QList<RowOption> matches;
    for (const RowOption &profile : profiles) {
        const QString name = profile.label.simplified().toLower();
        if (!name.isEmpty()
            && (wanted == name || wanted == QString(name).replace(QLatin1Char(' '), QLatin1Char('-')))) {
            matches << profile;
        }
    }
    if (matches.size() == 1) {
        return matches.first().id;
    }
    if (matches.size() > 1) {
        QStringList described;
        for (const RowOption &match : std::as_const(matches)) {
            described << QStringLiteral("%1 (%2)").arg(match.label,
                                                       isBuiltInWritingProfile(match.id) ? QStringLiteral("built-in")
                                                                                         : QStringLiteral("custom"));
        }
        *error = QStringLiteral("Writing profile %1 matches more than one profile: %2. Rename one of them.")
                     .arg(value, described.join(QStringLiteral(", ")));
        return std::nullopt;
    }
    for (const RowOption &profile : profiles) {
        if (profile.id == value) {
            return profile.id;
        }
    }
    *error = QStringLiteral("Unknown writing profile: %1 (expected %2)").arg(value, writingProfileNames(profiles));
    return std::nullopt;
}

// Lists the providers from the registry the app builds; registering creates
// no provider, so this is cheap and needs no credentials.
QString helpText()
{
    ProviderRegistry registry;
    registerProviders(registry, nullptr, nullptr);
    const QString separator = QStringLiteral(", ");
    return QString::fromUtf8(kHelp)
        .arg(providerIds(registry.speechProviders()).join(separator),
             providerIds(registry.refinementProviders()).join(separator),
             cliNames(cleanupNames()).join(separator),
             writingProfileNames(writingProfiles()),
             cliNames(toneNames()).join(separator));
}

// The stored id for a command-line name, or nothing for a name not offered.
std::optional<QString> storedId(const CliNames &choices, const QString &name)
{
    const qsizetype index = cliNames(choices).indexOf(name.toLower());
    return index < 0 ? std::nullopt : std::optional(choices.at(index).first);
}

// A Spoken Language a command line names, by code.
std::optional<QString> spokenLanguageNamed(const QString &value, QString *error)
{
    const QString code = value.trimmed().toLower();
    if (isKnownSpokenLanguage(code)) {
        return code;
    }
    *error = QStringLiteral("Unknown spoken language: %1 (expected a language code such as de, or auto)").arg(value);
    return std::nullopt;
}

std::optional<QString> spokenLanguageOption(const QStringList &arguments, QString *error)
{
    const qsizetype optionIndex = arguments.indexOf(QStringLiteral("--language"));
    if (optionIndex < 0) {
        return std::nullopt;
    }
    if (optionIndex + 1 >= arguments.size()) {
        *error = QStringLiteral("--language requires a language code such as de, or auto");
        return std::nullopt;
    }
    return spokenLanguageNamed(arguments.at(optionIndex + 1), error);
}

std::optional<QString> requestedWritingProfile(const QStringList &arguments, QString *error)
{
    const qsizetype optionIndex = arguments.indexOf(QStringLiteral("--profile"));
    if (optionIndex < 0) {
        return std::nullopt;
    }
    if (optionIndex + 1 >= arguments.size()) {
        *error = QStringLiteral("--profile requires one of %1").arg(writingProfileNames(writingProfiles()));
        return std::nullopt;
    }
    return writingProfileNamed(arguments.at(optionIndex + 1), error);
}

// The options that hand a session's overrides to the daemon a command starts.
QStringList sessionOverrideArguments(const SessionOverrides &overrides)
{
    QStringList arguments;
    if (overrides.outputFormat) {
        arguments << QStringLiteral("--format") << outputFormatName(*overrides.outputFormat);
    }
    if (overrides.writingProfile) {
        arguments << QStringLiteral("--profile") << *overrides.writingProfile;
    }
    if (overrides.spokenLanguage) {
        arguments << QStringLiteral("--language") << *overrides.spokenLanguage;
    }
    return arguments;
}

// The argument after index, which it moves past, or nothing at the end.
std::optional<QString> takeValue(const QStringList &arguments, qsizetype &index)
{
    if (index + 1 < arguments.size()) {
        return arguments.at(++index);
    }
    return std::nullopt;
}

// Reads one of the choices transcribe and listen share, the one at index,
// into options, moving index past its value. Returns false for an option that
// is not one of them; sets error for a usage mistake.
bool readSharedChoice(const QStringList &arguments,
                      qsizetype &index,
                      HeadlessTranscribeOptions &options,
                      QString *error)
{
    const QString argument = arguments.at(index);
    const auto value = [&] { return takeValue(arguments, index); };
    const auto choice = [&](const CliNames &choices, std::optional<QString> *target) {
        const std::optional<QString> given = value();
        if (!given) {
            *error = QStringLiteral("%1 requires a value").arg(argument);
            return;
        }
        *target = storedId(choices, *given);
        if (!*target) {
            *error = QStringLiteral("Unknown %1 value: %2 (expected %3)")
                         .arg(argument, *given, cliNames(choices).join(QStringLiteral(", ")));
        }
    };
    if (argument == QStringLiteral("--no-vocabulary")) {
        options.applyVocabulary = false;
    } else if (argument == QStringLiteral("--raw")) {
        options.raw = true;
    } else if (argument == QStringLiteral("--json")) {
        options.json = true;
    } else if (argument == QStringLiteral("--model") || argument == QStringLiteral("--refine")) {
        // Checked against the registry once it exists; this only reads it.
        const std::optional<QString> given = value();
        if (!given) {
            *error = QStringLiteral("%1 requires a value").arg(argument);
        } else {
            (argument == QStringLiteral("--model") ? options.speechProviderId
                                                   : options.refinementProviderId) = given->toLower();
        }
    } else if (argument == QStringLiteral("--cleanup")) {
        choice(cleanupNames(), &options.cleanupStrength);
    } else if (argument == QStringLiteral("--profile")) {
        const std::optional<QString> given = value();
        if (!given) {
            *error = QStringLiteral("--profile requires a value");
        } else {
            options.writingProfile = writingProfileNamed(*given, error);
        }
    } else if (argument == QStringLiteral("--language")) {
        const std::optional<QString> given = value();
        if (!given) {
            *error = QStringLiteral("--language requires a value");
        } else {
            options.spokenLanguage = spokenLanguageNamed(*given, error);
        }
    } else if (argument == QStringLiteral("--tone")) {
        choice(toneNames(), &options.tone);
    } else {
        return false;
    }
    return true;
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
        const auto value = [&] { return takeValue(arguments, index); };
        QString error;
        if (readSharedChoice(arguments, index, options, &error)) {
        } else if (argument == QStringLiteral("--headless")) {
        } else if (argument == QStringLiteral("--stdout")) {
            options.printTranscripts = true;
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

// Reads `speecher listen`'s arguments. Returns an error message for a usage
// mistake.
QString parseListenArguments(const QStringList &arguments, CommandLineDecision *decision)
{
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        QString error;
        if (argument == QStringLiteral("--until-silence")) {
            // The seconds are optional, and listen takes no other plain
            // argument they could be mistaken for.
            double seconds = kDefaultUntilSilenceSeconds;
            if (index + 1 < arguments.size() && !arguments.at(index + 1).startsWith(QLatin1Char('-'))) {
                bool ok = false;
                seconds = arguments.at(++index).toDouble(&ok);
                // Written so that nan, which fails every comparison, fails it.
                if (!(ok && seconds >= kMinUntilSilenceSeconds && seconds <= kMaxUntilSilenceSeconds)) {
                    return QStringLiteral("--until-silence takes seconds from %1 to %2, not %3")
                        .arg(kMinUntilSilenceSeconds)
                        .arg(kMaxUntilSilenceSeconds)
                        .arg(arguments.at(index));
                }
            }
            decision->untilSilenceMs = int(seconds * 1000);
        } else if (!readSharedChoice(arguments, index, decision->headless, &error)) {
            error = QStringLiteral("Unknown listen option: %1").arg(argument);
        }
        if (!error.isEmpty()) {
            return error;
        }
    }
    decision->mode = LaunchMode::ListenHeadless;
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
        || verb == QStringLiteral("cancel")
        || verb == QStringLiteral("status")
        || verb == QStringLiteral("settings")
        || verb == QStringLiteral("setup")
        || verb == QStringLiteral("grab")
        || verb == QStringLiteral("quit");
    decision.startListening = arguments.contains(QStringLiteral("--start-listening"));
    decision.showSettings = arguments.contains(QStringLiteral("--show-settings"));
    decision.showSetup = arguments.contains(QStringLiteral("--show-setup"));

    QString overrideError;
    SessionOverrides &overrides = decision.sessionOverrides;
    overrides.outputFormat = requestedOutputFormat(arguments, &overrideError);
    // transcribe and listen read their own --profile and --language.
    if (overrideError.isEmpty() && verb != QStringLiteral("transcribe") && verb != QStringLiteral("listen")) {
        overrides.writingProfile = requestedWritingProfile(arguments, &overrideError);
        if (overrideError.isEmpty()) {
            overrides.spokenLanguage = spokenLanguageOption(arguments, &overrideError);
        }
    }
    if (!overrideError.isEmpty()) {
        std::cerr << overrideError.toStdString() << "\n";
        return {LaunchMode::Exit, 2};
    }

    if (isCliCommand) {
        if (verb != QStringLiteral("toggle") && verb != QStringLiteral("start")) {
            if (overrides.outputFormat) {
                std::cerr << "--format can only be used with toggle or start\n";
                return {LaunchMode::Exit, 2};
            }
            if (overrides.writingProfile) {
                std::cerr << "--profile can only be used with toggle or start\n";
                return {LaunchMode::Exit, 2};
            }
            if (overrides.spokenLanguage) {
                std::cerr << "--language can only be used with toggle or start\n";
                return {LaunchMode::Exit, 2};
            }
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
    } else if (verb == QStringLiteral("listen")) {
        const QString error = parseListenArguments(arguments.mid(2), &decision);
        if (!error.isEmpty()) {
            std::cerr << error.toStdString() << "\n\n"
                      << helpText().toStdString();
            return {LaunchMode::Exit, 2};
        }
        return decision;
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
    // The session overrides only go with --start-listening, and a deleted
    // profile would make the relaunch refuse to start.
    static const QStringList startupOptions{QStringLiteral("--format"), QStringLiteral("--profile"),
                                            QStringLiteral("--language")};
    QStringList kept;
    kept.reserve(arguments.size());
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        if (startupOptions.contains(arguments.at(index))) {
            ++index;
        } else if (!startupActions.contains(arguments.at(index))) {
            kept << arguments.at(index);
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
                                                                              decision.sessionOverrides,
                                                                              &response,
                                                                              2500,
                                                                              platform,
                                                                              &ipcError);
    if (ipcResult == IpcCommandResult::Sent) {
        const SessionOverrides &overrides = decision.sessionOverrides;
        const bool ignoredProfile = overrides.writingProfile && response.writingProfile != *overrides.writingProfile;
        const bool ignoredLanguage = overrides.spokenLanguage && response.spokenLanguage != *overrides.spokenLanguage;
        if (response.ok && (ignoredProfile || ignoredLanguage)) {
            std::cerr << "The running Speecher is older and ignored " << (ignoredProfile ? "--profile" : "--language")
                      << ". Quit it with `speecher quit` and run the command again.\n";
            return 1;
        }
        std::cout << response.state.toStdString() << "\n";
        return response.ok ? 0 : 1;
    }
    if (ipcResult != IpcCommandResult::Unavailable) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if (command == QStringLiteral("stop") || command == QStringLiteral("cancel")
        || command == QStringLiteral("status")
        || command == QStringLiteral("quit") || command == QStringLiteral("grab")) {
        std::cout << "idle\n";
        return 0;
    }
    const bool started = command == QStringLiteral("showSettings")
        ? startDetachedSettings(platform.get())
        : command == QStringLiteral("showSetup")
            ? startDetachedSetup(platform.get())
            : startDetachedListening(platform.get(), sessionOverrideArguments(decision.sessionOverrides));
    if (!started) {
        std::cerr << "Could not start speecher daemon\n";
        return 1;
    }
    return 0;
}

} // namespace speecher
