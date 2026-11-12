#pragma once

#include "providers/GptLiveProtocol.h"

#include <QJsonObject>
#include <QString>

#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>

namespace speecher {

class EchoCanceller;
class GptLiveSession;
class PlatformComposition;
class SettingsStore;

// The version of the events and commands docs/live-protocol.md describes. It
// changes only when a change would break a host written against it; adding
// an event or a field does not.
inline constexpr int kLiveProtocolVersion = 1;

// What `speecher live` was asked for.
struct LiveOptions {
    // An audio file, or kStdinFile for raw s16le mono PCM streamed on stdin.
    // Empty with hostProtocol, whose audio comes in its commands.
    QString input;
    // The rate of stdin's PCM, or of the host's input.audio.
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
    // The host protocol: JSON commands on stdin, JSON events on stdout, and
    // the session runs until the host ends it or stdin does. Implies json.
    bool hostProtocol = false;
    // Takes the assistant's audio, as played, out of the input.
    bool echoCancel = false;
};

// Reads up to size bytes of the input into buffer: how many, 0 at its end.
// It may block; runLiveSession calls it on a thread of its own.
using LiveInputReader = std::function<qint64(char *buffer, qint64 size)>;

// The process's stdin, read without stdio, whose lock a blocked read would
// hold and exit() would wait on for as long as the pipe stays open.
LiveInputReader stdinReader();

// The JSON line for event, or nothing for an event a host has no use for.
// Audio goes in it only when withAudio, and silence never.
std::optional<QJsonObject> liveEventJson(const GptLiveEvent &event, bool withAudio);

// Talks to session with the input, printing the conversation to out (and the
// assistant's audio where options say), progress and failures to err. Without
// hostProtocol the session closes once the input has all gone out and the
// assistant answered the last thing said, or once nothing was said for
// options.lingerMs. With json the last line is always session.closed. echo,
// when given, takes the assistant's audio out of the input. Returns the exit
// code: 0 when the session ran and closed, 1 when it failed or out took
// nothing.
int runLiveSession(const LiveOptions &options, GptLiveSession &session, const QString &accessToken,
                   const QString &accountId, const LiveInputReader &in, std::ostream &out, std::ostream &err,
                   EchoCanceller *echo = nullptr);

// `speecher live`: signs in with the Codex ChatGPT login in settings and runs
// a session over WebRTC. 1 when the sign-in is missing or this build has no
// GPT-Live.
int runLiveCommand(const LiveOptions &options, SettingsStore *settings,
                   const std::shared_ptr<const PlatformComposition> &platform, std::ostream &out, std::ostream &err);

} // namespace speecher
