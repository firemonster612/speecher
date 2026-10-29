#pragma once

// The IPC wire format: the requests a command line sends the running
// instance and the responses it answers with, and how each reads from and
// writes to its newline-terminated JSON frame. Every release's peers speak
// this format, so a change here must still read and write what the previous
// release does.

#include "dictation/DictationTypes.h"
#include "recording/RecordingStatus.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

enum class IpcCommand {
    Toggle,
    Start,
    Stop,
    Cancel,
    ShowMain,
    ShowSettings,
    ShowSetup,
    Transcribe,
    RecordStart,
    RecordStatus,
    RecordStop,
    Status,
    WatchStatus,
    Last,
    Providers,
    AddVocabulary,
    Quit,
#ifdef SPEECHER_E2E_HOOKS
    E2eUpdateCheck,
    E2eUpdateStatus,
    E2ePause,
    E2eResume,
    E2eUpdateAccept,
#endif
};

// The command's name on the wire, and the command a name stands for.
QString ipcCommandName(IpcCommand command);
std::optional<IpcCommand> ipcCommandNamed(const QString &name);

struct IpcRequest {
    IpcCommand command = IpcCommand::Status;
    // toggle and start: the session's output format, Writing Profile and
    // Spoken Language.
    SessionOverrides overrides;
    // transcribe: the files to open. recordStart: the file to write, or none
    // for the default one.
    QStringList files;
    // addVocabulary: the terms to save. recordStart: terms for that
    // recording only.
    QStringList terms;
    // recordStart's --mic-only.
    bool microphoneOnly = false;

    bool operator==(const IpcRequest &other) const = default;
};

struct IpcResponse {
    bool ok = false;
    QString state;
    QString message;
    // The Writing Profile id and Spoken Language a toggle or start request
    // carried, echoed so a client can tell an instance that predates
    // --profile or --language, which ignores them.
    QString writingProfile;
    QString spokenLanguage;
    // The last transcript, in reply to the last command.
    QString text;
    // The terms an addVocabulary request carried that the list held already.
    QStringList skippedTerms;
    // The record commands' answer: the recording now, or the one stopped.
    std::optional<RecordingStatus> recording;
    // The instance did not know the command, so it is older than the
    // client. On the wire this is the message every release refuses an
    // unknown command with, which replaces message.
    bool unknownCommand = false;
};

QByteArray ipcRequestFrame(const IpcRequest &request);
// Reads a request frame, with or without its newline. When it is not one the
// instance can serve, returns nothing and sets refusal to the answer it gets;
// the refusal's state is left for the instance to fill in.
std::optional<IpcRequest> ipcRequestFromFrame(const QByteArray &frame, IpcResponse *refusal);

QByteArray ipcResponseFrame(const IpcResponse &response);
// Reads a response frame without its newline. Nothing when it is not a JSON
// object.
std::optional<IpcResponse> ipcResponseFromFrame(const QByteArray &frame);

} // namespace speecher
