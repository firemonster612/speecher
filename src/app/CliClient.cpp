#include "app/CliClient.h"

#include "app/PlatformComposition.h"
#include "app/SingleInstanceIpc.h"
#include "core/settings/SettingsCodecs.h"
#include "dictation/DictationTypes.h"
#include "recording/RecordingPresentation.h"
#include "recording/RecordingStatus.h"

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QThread>

#include <iostream>

namespace speecher {
namespace {

// record start may wait for macOS to ask about the microphone and for the
// speech provider to connect, and record stop for the provider's last
// utterances, with time to spare for the answer.
constexpr int kRecordStartTimeoutMs = 60000;
constexpr int kRecordStopTimeoutMs = kLongestRecordingStopMs + 5000;
// How long record start waits for the Speecher it started to answer.
constexpr int kDaemonStartTimeoutMs = 10000;
constexpr int kDaemonPollMs = 100;
// How often status --watch looks for a Speecher to start while none runs.
constexpr int kWatchConnectRetryMs = 2000;

// Starts a Speecher that outlives this command. Its standard streams go to the
// null device: holding this command's would keep a caller's
// `path=$(speecher record start)` waiting until that Speecher quits.
bool startDetachedSpeecher(const SingleInstancePlatform *platform, const QStringList &arguments)
{
    QProcess process;
    process.setProgram(platform->detachedExecutablePath());
    process.setArguments(arguments);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    return process.startDetached();
}

bool startDetachedListening(const SingleInstancePlatform *platform, const QStringList &overrideArguments)
{
    return startDetachedSpeecher(
        platform, QStringList{QStringLiteral("--daemon"), QStringLiteral("--start-listening")} + overrideArguments);
}

bool startDetachedDaemon(const SingleInstancePlatform *platform)
{
    return startDetachedSpeecher(platform, {QStringLiteral("--daemon")});
}

bool startDetachedSettings(const SingleInstancePlatform *platform)
{
    return startDetachedSpeecher(platform, {QStringLiteral("--daemon"), QStringLiteral("--show-settings")});
}

bool startDetachedSetup(const SingleInstancePlatform *platform)
{
    return startDetachedSpeecher(platform, {QStringLiteral("--daemon"), QStringLiteral("--show-setup")});
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

int reportOlderInstance(const char *problem)
{
    std::cerr << "The running Speecher is older and " << problem
              << ". Quit it with `speecher quit` and run the command again.\n";
    return 1;
}

// Prints the running instance's last transcript, or nothing when it has none
// or there is no running instance.
int printLastTranscript(const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    IpcResponse response;
    QString ipcError;
    const IpcCommandResult ipcResult =
        SingleInstanceIpc::sendRequest({IpcCommand::Last}, &response, kIpcTimeoutMs, platform, &ipcError);
    if (ipcResult == IpcCommandResult::Unavailable) {
        return 1;
    }
    if (ipcResult != IpcCommandResult::Sent) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }
    if (response.unknownCommand) {
        return reportOlderInstance("doesn't know `last`");
    }
    if (response.text.isEmpty()) {
        return 1;
    }
    std::cout << response.text.toStdString() << "\n";
    return 0;
}

// Saves the terms through the running instance, so its settings and its next
// dictation have them, or straight to the settings when none is running.
// Names each term the list held already on stderr.
int addVocabularyTerms(const QStringList &terms, const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    IpcResponse response;
    QString ipcError;
    IpcRequest request{IpcCommand::AddVocabulary};
    request.terms = terms;
    const IpcCommandResult ipcResult =
        SingleInstanceIpc::sendRequest(request, &response, kIpcTimeoutMs, platform, &ipcError);
    std::optional<QStringList> held;
    if (ipcResult == IpcCommandResult::Unavailable) {
        held = SettingsCodecs::addVocabularyTerms(terms);
    } else if (ipcResult != IpcCommandResult::Sent) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    } else if (response.unknownCommand) {
        return reportOlderInstance("doesn't know `vocabulary add`");
    } else if (response.ok) {
        held = response.skippedTerms;
    }
    if (!held) {
        std::cerr << "Could not save the vocabulary to Speecher's settings.\n";
        return 1;
    }
    for (const QString &term : std::as_const(*held)) {
        std::cerr << "Already in the vocabulary: " << term.toStdString() << "\n";
    }
    return 0;
}

// What status says when no Speecher runs.
IpcResponse idleStatus()
{
    return {true, dictationStateName(DictationState::Idle), {}};
}

// status --json's object: the dictation state, and the recording's status or
// false. An instance older than recording sends none, and records nothing.
QJsonObject statusJson(const IpcResponse &status)
{
    const RecordingStatus recording = status.recording.value_or(RecordingStatus());
    return {{QStringLiteral("state"), status.state},
            {QStringLiteral("recording"),
             recording.recording ? QJsonValue(recordingStatusJson(recording)) : QJsonValue(false)}};
}

QString statusLine(const IpcResponse &status, bool json)
{
    return json ? QString::fromUtf8(QJsonDocument(statusJson(status)).toJson(QJsonDocument::Compact))
                : statusWatchText(status.state, status.recording.value_or(RecordingStatus()));
}

int printStatusJson(const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    IpcResponse response;
    QString ipcError;
    const IpcCommandResult result =
        SingleInstanceIpc::sendRequest({IpcCommand::Status}, &response, kIpcTimeoutMs, platform, &ipcError);
    if (result == IpcCommandResult::Unavailable) {
        response = idleStatus();
    } else if (result != IpcCommandResult::Sent) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }
    std::cout << statusLine(response, true).toStdString() << "\n";
    return response.ok ? 0 : 1;
}

// status --watch: a line for the status now and one for each change, until
// the running Speecher quits or nothing reads the lines any more. Before one
// starts it prints the idle status and keeps looking for one, so a status bar
// started first picks it up.
int watchStatus(bool json, const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    QString printed;
    // Whether stdout still takes lines. Qt ignores SIGPIPE, so a reader
    // that went away, as `| head -1` does, shows only as a failed write.
    const auto print = [&](const IpcResponse &status) {
        // A line the same as the last carries nothing new for a status
        // bar, so it is not printed again: in text a change the line does
        // not show, such as the idle status the Speecher that just started
        // answers; with --json only an identical object, so a changed
        // problem still prints.
        const QString line = statusLine(status, json);
        if (line == printed) {
            return true;
        }
        printed = line;
        // Flushed: a status bar reads each line as it comes.
        std::cout << line.toStdString() << std::endl;
        return bool(std::cout);
    };
    std::optional<IpcResponse> refusal;
    const auto onStatus = [&](const IpcResponse &status) {
        if (!status.ok) {
            refusal = status;
            return true;
        }
        return print(status);
    };
    // Until it first answers, a Speecher that takes the connection but
    // writes nothing is starting, quitting or has every slot taken, so it is
    // tried again. It runs, so it is not idle: the idle status is printed
    // only while none runs. Once it has answered, the watch ends when it
    // closes the connection.
    QString ipcError;
    IpcCommandResult result;
    while ((result = SingleInstanceIpc::watchStatus(onStatus, kIpcTimeoutMs, platform, &ipcError))
               == IpcCommandResult::Unavailable
           || result == IpcCommandResult::NoResponse) {
        if (result == IpcCommandResult::Unavailable && !print(idleStatus())) {
            return 0;
        }
        QThread::msleep(kWatchConnectRetryMs);
    }
    if (result != IpcCommandResult::Sent) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }
    if (refusal && refusal->unknownCommand) {
        return reportOlderInstance("doesn't know `status --watch`");
    }
    if (refusal && !refusal->message.isEmpty()) {
        std::cerr << refusal->message.toStdString() << "\n";
        return 1;
    }
    return 0;
}

// Prints what went wrong with each stream and with the file on stderr.
// Returns whether anything did.
bool printRecordingProblems(const RecordingStatus &status)
{
    QStringList problems;
    for (const RecordingStreamStatus &stream : status.streams) {
        problems << recordingStreamProblemText(stream);
    }
    problems << recordingWriteProblemText(status);
    problems.removeAll(QString());
    for (const QString &problem : std::as_const(problems)) {
        std::cerr << problem.toStdString() << "\n";
    }
    return !problems.isEmpty();
}

// record status's output: exit status 1 when nothing is recording.
int printRecordingStatus(const RecordingStatus &status, bool json)
{
    std::cout << (json ? QJsonDocument(recordingStatusJson(status)).toJson(QJsonDocument::Compact).toStdString()
                       : recordingStatusText(status).toStdString())
              << "\n";
    printRecordingProblems(status);
    return status.recording ? 0 : 1;
}

// The record commands, which print what the recording says rather than the
// dictation state. record start starts Speecher when none is running and
// waits for it to answer, since only it knows the file it writes.
int runRecordCommand(const CommandLineDecision &decision,
                     const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    const IpcCommand command = decision.ipcCommand;
    const bool start = command == IpcCommand::RecordStart;
    const int timeoutMs = start ? kRecordStartTimeoutMs
        : command == IpcCommand::RecordStop ? kRecordStopTimeoutMs
                                            : kIpcTimeoutMs;
    IpcRequest request{command};
    if (start) {
        if (!decision.recordPath.isEmpty()) {
            request.files = {decision.recordPath};
        }
        request.terms = decision.vocabularyTerms;
        request.microphoneOnly = decision.recordMicrophoneOnly;
    }
    IpcResponse response;
    QString ipcError;
    const auto send = [&] {
        return SingleInstanceIpc::sendRequest(request, &response, timeoutMs, platform, &ipcError);
    };
    IpcCommandResult result = send();
    if (result == IpcCommandResult::Unavailable && start) {
        if (!startDetachedDaemon(platform.get())) {
            std::cerr << "Could not start speecher daemon\n";
            return 1;
        }
        QDeadlineTimer deadline(kDaemonStartTimeoutMs);
        while (result == IpcCommandResult::Unavailable && !deadline.hasExpired()) {
            QThread::msleep(kDaemonPollMs);
            result = send();
        }
    }
    if (result == IpcCommandResult::Unavailable) {
        if (command == IpcCommand::RecordStatus) {
            return printRecordingStatus({}, decision.json);
        }
        std::cerr << (start ? QStringLiteral("Speecher did not start") : notRecordingText()).toStdString() << "\n";
        return 1;
    }
    if (result != IpcCommandResult::Sent) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }
    if (!response.ok) {
        if (response.unknownCommand) {
            return reportOlderInstance("cannot record");
        }
        std::cerr << response.message.toStdString() << "\n";
        return 1;
    }
    const RecordingStatus status = response.recording.value_or(RecordingStatus());
    if (command == IpcCommand::RecordStatus) {
        return printRecordingStatus(status, decision.json);
    }
    // record start's message is the consent notice, on the first one ever.
    if (!response.message.isEmpty()) {
        std::cerr << response.message.toStdString() << "\n";
    }
    // A recording that started with a problem, such as system audio that
    // failed, runs on; a stopped one that missed something is still where it
    // is, but incomplete.
    const bool incomplete = printRecordingProblems(status);
    if (start && !status.echoCancellationWarning.isEmpty()) {
        std::cerr << status.echoCancellationWarning.toStdString() << "\n";
    }
    std::cout << status.path.toStdString() << "\n";
    return incomplete && !start ? 1 : 0;
}

} // namespace

int runCliCommand(const CommandLineDecision &decision,
                  const std::shared_ptr<const SingleInstancePlatform> &platform)
{
    const IpcCommand command = decision.ipcCommand;
    switch (command) {
    case IpcCommand::Last:
        return printLastTranscript(platform);
    case IpcCommand::AddVocabulary:
        return addVocabularyTerms(decision.vocabularyTerms, platform);
    case IpcCommand::RecordStart:
    case IpcCommand::RecordStatus:
    case IpcCommand::RecordStop:
        return runRecordCommand(decision, platform);
    case IpcCommand::WatchStatus:
        return watchStatus(decision.json, platform);
    default:
        break;
    }
    if (command == IpcCommand::Status && decision.json) {
        return printStatusJson(platform);
    }
    IpcResponse response;
    QString ipcError;
    const IpcCommandResult ipcResult = SingleInstanceIpc::sendRequest(
        {command, decision.sessionOverrides}, &response, kIpcTimeoutMs, platform, &ipcError);
    if (ipcResult == IpcCommandResult::Sent) {
        const SessionOverrides &overrides = decision.sessionOverrides;
        const bool ignoredProfile = overrides.writingProfile && response.writingProfile != *overrides.writingProfile;
        const bool ignoredLanguage = overrides.spokenLanguage && response.spokenLanguage != *overrides.spokenLanguage;
        if (response.ok && (ignoredProfile || ignoredLanguage)) {
            return reportOlderInstance(ignoredProfile ? "ignored --profile" : "ignored --language");
        }
        std::cout << response.state.toStdString() << "\n";
        return response.ok ? 0 : 1;
    }
    if (ipcResult != IpcCommandResult::Unavailable) {
        std::cerr << ipcError.toStdString() << "\n";
        return 1;
    }

    if (command == IpcCommand::Stop || command == IpcCommand::Cancel || command == IpcCommand::Status
        || command == IpcCommand::Quit) {
        std::cout << "idle\n";
        return 0;
    }
    const bool started = command == IpcCommand::ShowSettings
        ? startDetachedSettings(platform.get())
        : command == IpcCommand::ShowSetup
            ? startDetachedSetup(platform.get())
            : startDetachedListening(platform.get(), sessionOverrideArguments(decision.sessionOverrides));
    if (!started) {
        std::cerr << "Could not start speecher daemon\n";
        return 1;
    }
    return 0;
}

} // namespace speecher
