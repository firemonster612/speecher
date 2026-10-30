#include "app/CommandLine.h"

#include "app/ProviderSetup.h"
#include "app/ShellCompletions.h"
#include "core/Vocabulary.h"
#include "core/settings/SettingsCodecs.h"
#include "core/settings/SpokenLanguages.h"
#include "dictation/DictationTypes.h"
#include "providers/ProviderRegistry.h"
#include "transcribe/FileTranscriptionSession.h"

#include <QFileInfo>

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
    const QString value = arguments.at(optionIndex + 1);
    const std::optional<OutputFormat> format = outputFormatFromString(value);
    if (!format && error) {
        *error = QStringLiteral("Unknown output format: %1").arg(value.trimmed().toLower());
    }
    return format;
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
  status [--json] [--watch]
                           print the dictation state; --json prints one
                           object with the recording as record status --json
                           has it, or false; --watch prints a line now and on
                           each change until Ctrl-C, and waits for a Speecher
                           to start when none runs
  last                     print the last transcript
  vocabulary add [--] <terms...>
                           save the terms to the custom vocabulary
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
  --vocab-file <path>      add the file's terms, one per line, to the custom
                           vocabulary for this run, even with
                           --no-vocabulary; # starts a comment line
  --refine <id|none>       refinement provider: %2, none
  --cleanup <level>        %3
  --profile <name>         writing profile; seeds its services, cleanup and
                           tone: %4
  --tone <name>            %5
  --language <code>        spoken language: a code such as de, or auto
  --output <beside|none|DIR>
                           where to save <name>-transcribed.txt (default beside)
  -                        as the only file, read the audio from stdin; needs
                           --stdout, --json or --output DIR, and saves as
                           stdin-transcribed.txt
  --stdout                 also print each transcript
  --raw                    print and save the raw transcript, not the refined one
  --srt | --vtt            save and print SRT or WebVTT subtitles instead, from
                           the raw transcript; only local models give timings
  --json                   print one JSON object per file, then a summary
  Exit status: 0 all files transcribed, 1 some failed, 2 usage error.

Record from the microphone once and print what was said:
  speecher listen [options]
  --until-silence [seconds]
                           also stop after this much silence once speech has
                           started (default 2); Ctrl-C, and Enter at a
                           terminal, always stop, keeping what was said
  Takes --model, --no-vocabulary, --vocab-file, --refine, --cleanup,
  --profile, --tone, --language, --raw and --json as transcribe does; --json
  prints one object.
  Exit status: 0 transcript printed, 1 failed or heard no speech, 2 usage
  error.

Check which speech and refinement services can work:
  speecher providers [--json]
                           list each provider: configured, signed in and
                           usable, as Speecher's settings judge them; asks the
                           running Speecher, and asks no server, so a sign-in
                           nothing has seen is unknown
  --json                   print a JSON array of objects instead of a table

Record a call into a file, in the running Speecher (started if needed):
  speecher record start [--to <file>] [--mic-only] [--vocab-file <path>]
                           record the microphone as "me" and, on Linux and
                           Windows, system audio as "them", and print the
                           file's path once the speech provider is
                           connected, or exit with status 1 and no file;
                           each utterance is appended as one line,
                           "[hh:mm:ss] me: text" or "[hh:mm:ss] them: text"
                           (default file: recordings/<yyyy-mm-dd-hhmm>.md in
                           Speecher's data folder)
  --mic-only               record the microphone alone, without system audio
  --vocab-file <path>      add the file's terms to the custom vocabulary for
                           this recording, as transcribe does
  speecher record status [--json]
                           print the file, duration and streams; exit status
                           1 when not recording
  speecher record stop     write the last utterance, then print the file;
                           exit status 1 when the recording missed something,
                           which it says on stderr

Complete speecher's commands and options in a shell:
  speecher completions bash|zsh|fish
                           print the shell's completion script; it asks
                           speecher for the profile, tone and cleanup names
                           each time it completes them

Options:
  --format plain|html      output format for toggle and start
  --profile <name>         writing profile for toggle and start: %4
  --language <code>        spoken language for toggle and start: a code such
                           as de, or auto
  --daemon                 run without a window
  --version                print the version
  --help                   print this help
)";

// Each stored id and the name the command line gives it. Made from the ids
// rather than the settings labels, so renaming a label never changes the
// command line.
using CliNames = QList<std::pair<QString, QString>>;

// The cleanup levels the command line names by how much they change.
const QHash<QString, QString> kCleanupCliNames{{QStringLiteral("light_cleanup"), QStringLiteral("light")},
                                               {QStringLiteral("balanced"), QStringLiteral("medium")},
                                               {QStringLiteral("strong_polish"), QStringLiteral("high")}};

// The built-in ids, then each custom tone or level the settings hold, each
// named by its id without custom_ and with - for _ unless renamed says
// otherwise.
template<typename Custom>
CliNames cliNamesFor(const QStringList &builtIn, const QList<Custom> &custom,
                     const QHash<QString, QString> &renamed = {})
{
    const auto name = [](const QString &id) {
        QString stripped = id.startsWith(kCustomIdPrefix) ? id.mid(kCustomIdPrefix.size()) : id;
        return stripped.replace(QLatin1Char('_'), QLatin1Char('-'));
    };
    CliNames names;
    for (const QString &id : builtIn) {
        names.append({id, renamed.value(id, name(id))});
    }
    for (const Custom &choice : custom) {
        if (choice.id.startsWith(kCustomIdPrefix)) {
            names.append({choice.id, name(choice.id)});
        }
    }
    return names;
}

CliNames cleanupNames()
{
    return cliNamesFor(kBuiltInCleanupLevels, SettingsCodecs().customCleanupLevels(), kCleanupCliNames);
}

CliNames toneNames()
{
    return cliNamesFor(kBuiltInTones, SettingsCodecs().customTones());
}

QStringList cliNames(const CliNames &choices)
{
    QStringList names;
    for (const auto &[id, name] : choices) {
        names << name;
    }
    return names;
}

// Alphabetical: the registry orders providers by label.
QStringList sortedProviderIds(const QList<ProviderDescriptor> &providers)
{
    QStringList ids = providerIds(providers);
    ids.sort();
    return ids;
}

// A Writing Profile as the command line names it: a built-in's label or a
// custom profile's name.
struct NamedProfile {
    QString id;
    QString label;
};

// The built-in profiles, then the custom ones the settings hold.
QList<NamedProfile> writingProfiles()
{
    const QList<WritingProfileSettings> saved = SettingsCodecs().writingProfileSettings();
    QList<NamedProfile> profiles;
    for (const WritingProfileSettings &builtIn : defaultWritingProfileSettings()) {
        profiles.append({builtIn.profile, writingProfileLabel(builtIn.profile, {})});
    }
    for (const WritingProfileSettings &profile : saved) {
        if (!isBuiltInWritingProfile(profile.profile)) {
            profiles.append({profile.profile, profile.name});
        }
    }
    return profiles;
}

// The profiles' current names, for --help and errors. A name with a space is
// quoted, as a shell needs it.
QString writingProfileNames(const QList<NamedProfile> &profiles)
{
    QStringList names;
    for (const NamedProfile &profile : profiles) {
        names << (profile.label.contains(QLatin1Char(' ')) ? QStringLiteral("\"%1\"").arg(profile.label)
                                                           : profile.label);
    }
    return names.join(QStringLiteral(", "));
}

// A profile's name as the command line gives it: its words joined by -, so
// no shell splits it. --profile takes this or the name itself, in any case.
QString writingProfileCliName(const QString &label)
{
    return label.simplified().replace(QLatin1Char(' '), QLatin1Char('-'));
}

// The profiles a command-line value names by their current name, in any case,
// or that name with - between words.
QList<NamedProfile> writingProfilesNamed(const QList<NamedProfile> &profiles, const QString &value)
{
    const QString wanted = value.trimmed().toLower();
    QList<NamedProfile> matches;
    for (const NamedProfile &profile : profiles) {
        const QString name = profile.label.simplified().toLower();
        if (!name.isEmpty() && (wanted == name || wanted == writingProfileCliName(name))) {
            matches << profile;
        }
    }
    return matches;
}

// The id of the Writing Profile a command-line value names: a profile's name,
// as writingProfilesNamed matches it. Failing that, an exact id, which is how
// a command hands the profile to the daemon it starts. Sets error when the
// value names no profile or more than one.
std::optional<QString> writingProfileNamed(const QString &value, QString *error)
{
    const QList<NamedProfile> profiles = writingProfiles();
    const QList<NamedProfile> matches = writingProfilesNamed(profiles, value);
    if (matches.size() == 1) {
        return matches.first().id;
    }
    if (matches.size() > 1) {
        QStringList described;
        for (const NamedProfile &match : std::as_const(matches)) {
            described << QStringLiteral("%1 (%2)").arg(match.label,
                                                       isBuiltInWritingProfile(match.id) ? QStringLiteral("built-in")
                                                                                         : QStringLiteral("custom"));
        }
        *error = QStringLiteral("Writing profile %1 matches more than one profile: %2. Rename one of them.")
                     .arg(value, described.join(QStringLiteral(", ")));
        return std::nullopt;
    }
    for (const NamedProfile &profile : profiles) {
        if (profile.id == value) {
            return profile.id;
        }
    }
    *error = QStringLiteral("Unknown writing profile: %1 (expected %2)").arg(value, writingProfileNames(profiles));
    return std::nullopt;
}

// The name `completions --list profiles` offers for each profile, one that
// --profile takes for it alone: its command-line name, or its id where
// another profile has that name too or the name starts with -, as --help
// and --version do, which the command line reads before --profile.
QStringList writingProfileCompletions()
{
    const QList<NamedProfile> profiles = writingProfiles();
    QStringList names;
    for (const NamedProfile &profile : profiles) {
        const QString name = writingProfileCliName(profile.label);
        if (name.isEmpty()) {
            continue;
        }
        if (!name.startsWith(QLatin1Char('-')) && writingProfilesNamed(profiles, name).size() == 1) {
            names << name;
        } else if (writingProfilesNamed(profiles, profile.id).isEmpty()) {
            names << profile.id;
        }
    }
    return names;
}

// Lists the providers from the registry the app builds; registering creates
// no provider, so this is cheap and needs no credentials.
QString helpText()
{
    ProviderRegistry registry;
    registerProviders(registry, nullptr, nullptr);
    const QString separator = QStringLiteral(", ");
    return QString::fromUtf8(kHelp)
        .arg(sortedProviderIds(registry.speechProviders()).join(separator),
             sortedProviderIds(registry.refinementProviders()).join(separator),
             cliNames(cleanupNames()).join(separator),
             writingProfileNames(writingProfiles()),
             cliNames(toneNames()).join(separator));
}

// The choices a completion script carries, from the same registry as --help.
CompletionChoices completionChoices()
{
    ProviderRegistry registry;
    registerProviders(registry, nullptr, nullptr);
    return {sortedProviderIds(registry.speechProviders()), sortedProviderIds(registry.refinementProviders()),
            knownSpokenLanguages()};
}

// The names `completions --list` prints for one of the lists a completion
// script reads while completing, or nothing for another list.
std::optional<QStringList> completionList(const QString &list)
{
    if (list == kCompletionProfiles) {
        return writingProfileCompletions();
    }
    if (list == kCompletionTones) {
        return cliNames(toneNames());
    }
    if (list == kCompletionCleanupLevels) {
        return cliNames(cleanupNames());
    }
    return std::nullopt;
}

// `speecher completions <shell>` prints the shell's completion script, and
// `completions --list <list>`, which those scripts run, one name a line.
// Reads the settings without starting Speecher, so completing stays quick.
CommandLineDecision printCompletions(const QStringList &arguments)
{
    if (arguments.size() == 1 && kCompletionShells.contains(arguments.first())) {
        std::cout << completionScript(arguments.first(), completionChoices()).toStdString();
        return {LaunchMode::Exit};
    }
    const std::optional<QStringList> names = arguments.size() == 2 && arguments.first() == QStringLiteral("--list")
        ? completionList(arguments.last())
        : std::nullopt;
    if (names) {
        for (const QString &name : *names) {
            std::cout << name.toStdString() << "\n";
        }
        return {LaunchMode::Exit};
    }
    const QString shells = kCompletionShells.mid(0, kCompletionShells.size() - 1).join(QStringLiteral(", "))
        + QStringLiteral(" or ") + kCompletionShells.last();
    std::cerr << (arguments.isEmpty() ? QStringLiteral("completions needs a shell: %1").arg(shells)
                                      : QStringLiteral("Unknown completions argument: %1 (expected %2)")
                                            .arg(arguments.join(QLatin1Char(' ')), shells))
                     .toStdString()
              << "\n\n"
              << helpText().toStdString();
    return {LaunchMode::Exit, 2};
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

// The argument after index, which it moves past, or nothing at the end.
std::optional<QString> takeValue(const QStringList &arguments, qsizetype &index)
{
    if (index + 1 < arguments.size()) {
        return arguments.at(++index);
    }
    return std::nullopt;
}

// Adds the terms in --vocab-file's file to terms. Returns an error message
// when the option has no file or the file cannot be read.
QString addVocabularyFile(const std::optional<QString> &path, QStringList *terms)
{
    if (!path) {
        return QStringLiteral("--vocab-file requires a value");
    }
    const std::optional<QStringList> read = readVocabularyFile(*path);
    if (!read) {
        return QStringLiteral("Cannot read vocabulary file %1").arg(*path);
    }
    *terms += *read;
    return {};
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
    } else if (argument == QStringLiteral("--vocab-file")) {
        *error = addVocabularyFile(value(), &options.addedVocabulary);
    } else {
        return false;
    }
    return true;
}

// Finishes reading `speecher transcribe -`, which always runs headless and
// has no folder to save beside, so it saves nowhere unless given one. Returns
// an error message for a usage mistake.
QString finishStdinTranscribe(const QStringList &files, bool besideGiven, CommandLineDecision *decision)
{
    HeadlessTranscribeOptions &options = decision->headless;
    if (files.size() > 1) {
        return QStringLiteral("- reads stdin and must be the only file");
    }
    if (besideGiven) {
        return QStringLiteral("--output beside cannot be used with -, which has no folder");
    }
    if (options.destination == TranscriptDestination::BesideInput) {
        options.destination = TranscriptDestination::None;
    }
    if (!options.printTranscripts && !options.json && options.destination != TranscriptDestination::Folder) {
        return QStringLiteral("transcribe - needs --stdout, --json or --output <folder>");
    }
    decision->transcribeFiles = files;
    decision->mode = LaunchMode::TranscribeHeadless;
    return {};
}

// Reads `speecher transcribe`'s arguments. Returns an error message for a
// usage mistake.
QString parseTranscribeArguments(const QStringList &arguments, CommandLineDecision *decision)
{
    HeadlessTranscribeOptions &options = decision->headless;
    bool headless = false;
    bool besideGiven = false;
    QStringList files;
    bool optionsEnded = false;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (optionsEnded || !argument.startsWith(QLatin1Char('-')) || argument == kStdinFile) {
            files << argument;
            continue;
        }
        if (argument == QStringLiteral("--")) {
            optionsEnded = true;
            continue;
        }
        headless = true;
        const auto value = [&] { return takeValue(arguments, index); };
        QString error;
        if (readSharedChoice(arguments, index, options, &error)) {
        } else if (argument == QStringLiteral("--headless")) {
        } else if (argument == QStringLiteral("--stdout")) {
            options.printTranscripts = true;
        } else if (argument == QStringLiteral("--srt") || argument == QStringLiteral("--vtt")) {
            const TranscriptFormat format =
                argument == QStringLiteral("--srt") ? TranscriptFormat::Srt : TranscriptFormat::WebVtt;
            if (options.format != TranscriptFormat::Text && options.format != format) {
                error = QStringLiteral("--srt and --vtt cannot be used together");
            }
            options.format = format;
        } else if (argument == QStringLiteral("--output")) {
            const std::optional<QString> given = value();
            besideGiven = given && given->toLower() == QStringLiteral("beside");
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
    if (options.raw && options.format != TranscriptFormat::Text) {
        return QStringLiteral("--raw cannot be used with --srt or --vtt, whose subtitles always come from the raw "
                              "transcript");
    }
    if (files.isEmpty()) {
        return QStringLiteral("transcribe needs at least one audio file");
    }
    if (files.contains(kStdinFile)) {
        return finishStdinTranscribe(files, besideGiven, decision);
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
            // argument they could be mistaken for. A negative number is
            // still the seconds, given wrong, rather than an option.
            double seconds = kDefaultUntilSilenceSeconds;
            if (index + 1 < arguments.size()) {
                const QString next = arguments.at(index + 1);
                bool ok = false;
                const double given = next.toDouble(&ok);
                if (ok || !next.startsWith(QLatin1Char('-'))) {
                    ++index;
                    // Written so that nan, which fails every comparison, fails it.
                    if (!(ok && given >= kMinUntilSilenceSeconds && given <= kMaxUntilSilenceSeconds)) {
                        return QStringLiteral("--until-silence takes seconds from %1 to %2, not %3")
                            .arg(kMinUntilSilenceSeconds)
                            .arg(kMaxUntilSilenceSeconds)
                            .arg(next);
                    }
                    seconds = given;
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

// Reads `speecher vocabulary`'s arguments. Returns an error message for a
// usage mistake.
QString parseVocabularyArguments(const QStringList &arguments, CommandLineDecision *decision)
{
    if (arguments.isEmpty()) {
        return QStringLiteral("vocabulary needs a command: add");
    }
    if (arguments.first().toLower() != QStringLiteral("add")) {
        return QStringLiteral("Unknown vocabulary command: %1 (expected add)").arg(arguments.first());
    }
    QStringList terms;
    bool optionsEnded = false;
    for (const QString &argument : arguments.mid(1)) {
        if (!optionsEnded && argument == QStringLiteral("--")) {
            optionsEnded = true;
            continue;
        }
        if (!optionsEnded && argument.startsWith(QLatin1Char('-'))) {
            return QStringLiteral("Unknown vocabulary add option: %1 (put -- before a term that starts with -)")
                .arg(argument);
        }
        if (argument.simplified().isEmpty()) {
            return QStringLiteral("vocabulary add cannot save a blank term");
        }
        terms << argument;
    }
    if (terms.isEmpty()) {
        return QStringLiteral("vocabulary add needs at least one term");
    }
    decision->mode = LaunchMode::RunCli;
    decision->ipcCommand = IpcCommand::AddVocabulary;
    decision->vocabularyTerms = terms;
    return {};
}

// Reads `speecher record`'s subcommand and options. Returns an error message
// for a usage mistake.
QString parseRecordArguments(const QStringList &arguments, CommandLineDecision *decision)
{
    const QString subcommand = arguments.value(0).toLower();
    if (subcommand != QStringLiteral("start") && subcommand != QStringLiteral("status")
        && subcommand != QStringLiteral("stop")) {
        return QStringLiteral("record needs start, status or stop");
    }
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (subcommand == QStringLiteral("start") && argument == QStringLiteral("--to")) {
            const std::optional<QString> path = takeValue(arguments, index);
            if (!path || path->isEmpty()) {
                return QStringLiteral("--to requires a file");
            }
            if (QFileInfo(*path).isDir()) {
                return QStringLiteral("--to needs a file, not a folder: %1").arg(*path);
            }
            decision->recordPath = QFileInfo(*path).absoluteFilePath();
        } else if (subcommand == QStringLiteral("start") && argument == QStringLiteral("--mic-only")) {
            decision->recordMicrophoneOnly = true;
        } else if (subcommand == QStringLiteral("start") && argument == QStringLiteral("--vocab-file")) {
            if (const QString error = addVocabularyFile(takeValue(arguments, index), &decision->vocabularyTerms);
                !error.isEmpty()) {
                return error;
            }
        } else if (subcommand == QStringLiteral("status") && argument == QStringLiteral("--json")) {
            decision->json = true;
        } else {
            return QStringLiteral("Unknown record %1 option: %2").arg(subcommand, argument);
        }
    }
    decision->ipcCommand = subcommand == QStringLiteral("start") ? IpcCommand::RecordStart
        : subcommand == QStringLiteral("status")                 ? IpcCommand::RecordStatus
                                                                 : IpcCommand::RecordStop;
    return {};
}

// Reads `speecher status`'s options. Returns an error message for a usage
// mistake.
QString parseStatusArguments(const QStringList &arguments, CommandLineDecision *decision)
{
    decision->ipcCommand = IpcCommand::Status;
    for (const QString &argument : arguments) {
        if (argument == QStringLiteral("--json")) {
            decision->json = true;
        } else if (argument == QStringLiteral("--watch")) {
            decision->ipcCommand = IpcCommand::WatchStatus;
        } else {
            return QStringLiteral("Unknown status option: %1").arg(argument);
        }
    }
    return {};
}

} // namespace

CommandLineDecision parseCommandLine(const QStringList &arguments, const QString &logPath)
{
    // After --, an argument is a term or a file, never one of these.
    const QStringList options = arguments.mid(0, arguments.indexOf(QStringLiteral("--")));
    if (options.contains(QStringLiteral("--version"))) {
        std::cout << "speecher " << SPEECHER_VERSION << " (build " << SPEECHER_BUILD_NUMBER << ")\n";
        std::cout << "log " << logPath.toStdString() << "\n";
        return {LaunchMode::Exit};
    }

    if (options.contains(QStringLiteral("--help")) || options.contains(QStringLiteral("-h"))) {
        std::cout << helpText().toStdString();
        return {LaunchMode::Exit};
    }

    CommandLineDecision decision;

    const QString verb = arguments.size() >= 2 ? arguments.at(1).trimmed().toLower() : QString();
    if (verb == QStringLiteral("completions")) {
        return printCompletions(arguments.mid(2));
    }
    // Before the dictation options below, which vocabulary does not take.
    if (verb == QStringLiteral("vocabulary")) {
        const QString error = parseVocabularyArguments(arguments.mid(2), &decision);
        if (!error.isEmpty()) {
            std::cerr << error.toStdString() << "\n\n"
                      << helpText().toStdString();
            return {LaunchMode::Exit, 2};
        }
        return decision;
    }
    const bool isCliCommand = verb == QStringLiteral("toggle")
        || verb == QStringLiteral("start")
        || verb == QStringLiteral("stop")
        || verb == QStringLiteral("cancel")
        || verb == QStringLiteral("status")
        || verb == QStringLiteral("last")
        || verb == QStringLiteral("settings")
        || verb == QStringLiteral("setup")
        || verb == QStringLiteral("record")
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
        if (verb == QStringLiteral("record")) {
            const QString error = parseRecordArguments(arguments.mid(2), &decision);
            if (!error.isEmpty()) {
                std::cerr << error.toStdString() << "\n\n"
                          << helpText().toStdString();
                return {LaunchMode::Exit, 2};
            }
            return decision;
        }
        if (verb == QStringLiteral("status")) {
            const QString error = parseStatusArguments(arguments.mid(2), &decision);
            if (!error.isEmpty()) {
                std::cerr << error.toStdString() << "\n\n"
                          << helpText().toStdString();
                return {LaunchMode::Exit, 2};
            }
            return decision;
        }
        // The other verbs are their commands' names.
        decision.ipcCommand = verb == QStringLiteral("settings") ? IpcCommand::ShowSettings
            : verb == QStringLiteral("setup")                    ? IpcCommand::ShowSetup
                                                                 : ipcCommandNamed(verb).value();
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
    } else if (verb == QStringLiteral("providers")) {
        for (const QString &argument : arguments.mid(2)) {
            if (argument != QStringLiteral("--json")) {
                std::cerr << "Unknown providers option: " << argument.toStdString() << "\n\n"
                          << helpText().toStdString();
                return {LaunchMode::Exit, 2};
            }
            decision.json = true;
        }
        decision.mode = LaunchMode::ListProviders;
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

QStringList updateRelaunchArguments(const QStringList &arguments)
{
    const QString daemon = QStringLiteral("--daemon");
    return arguments.contains(daemon) ? QStringList{daemon} : QStringList{};
}

bool opensSetupAtLaunch(const CommandLineDecision &decision, bool setupPending)
{
    return decision.showSetup || (setupPending && decision.mode != LaunchMode::RunDaemon);
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

} // namespace speecher
