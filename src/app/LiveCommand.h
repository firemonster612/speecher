#pragma once

#include "providers/GptLiveProtocol.h"

#include <QJsonObject>
#include <QString>

#include <iosfwd>
#include <optional>

namespace speecher {

class GptLiveSession;
class SettingsStore;

// What `speecher live` was asked for.
struct LiveOptions {
    // An audio file, or kStdinFile for raw s16le mono PCM streamed on stdin.
    QString input;
    // stdin's sample rate.
    int stdinRate = 24000;
    GptLiveSessionOptions session;
    // Where the assistant's audio goes: nowhere when empty, raw 24 kHz s16le
    // mono on stdout for "-", otherwise a WAV file.
    QString audioOut;
    // One JSON object a line on stdout for each event, instead of a line for
    // each turn.
    bool json = false;
    // Once the input has all gone out, how long the session waits with
    // neither side saying anything before it closes.
    int lingerMs = 30000;
};

// The --json line for event, or nothing for an event a script has no use
// for. Audio goes in it only when withAudio.
std::optional<QJsonObject> liveEventJson(const GptLiveEvent &event, bool withAudio);

// Talks to session with the input, printing the conversation to out (and the
// assistant's audio where options say), progress and failures to err. The
// session closes once the input has all gone out and the assistant answered
// the last thing said, or once nothing was said for options.lingerMs. With
// json the last line is always session.closed. Returns the exit code: 0
// when the session ran and closed, 1 when it failed or out took nothing.
int runLiveSession(const LiveOptions &options, GptLiveSession &session, const QString &accessToken,
                   const QString &accountId, std::istream &in, std::ostream &out, std::ostream &err);

// `speecher live`: signs in with the Codex ChatGPT login in settings and runs
// a session over WebRTC. 1 when the sign-in is missing or this build has no
// GPT-Live.
int runLiveCommand(const LiveOptions &options, SettingsStore *settings, std::istream &in, std::ostream &out,
                   std::ostream &err);

} // namespace speecher
