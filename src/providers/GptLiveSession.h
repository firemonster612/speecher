#pragma once

#include "providers/GptLiveProtocol.h"

#include <QObject>

namespace speecher {

// One GPT-Live conversation: the caller's audio in, the call's events out.
// GptLiveCall is the real one; tests use their own.
class GptLiveSession : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Signs in with the Codex ChatGPT login and opens the call; events come
    // from SessionStarted on, which comes once the session takes audio in as
    // fast as it is spoken, unless options.acceptSlowSession.
    virtual void start(const QString &accessToken, const QString &accountId,
                       const GptLiveSessionOptions &options) = 0;
    // 16 kHz mono s16le, as it was heard. The session sends it on at the
    // pace it was spoken, so a file's audio waits here.
    virtual void sendAudio(const QByteArray &pcm16k) = 0;
    // How much of the audio sent waits to go out, in milliseconds.
    virtual qint64 queuedMs() const = 0;
    // Text the assistant acts on as if told it; see gptLiveContextAppend.
    virtual void sendContext(const QString &text) = 0;
    // Stops or resumes the session hearing the caller, as for a host that
    // has no echo control and plays the assistant aloud.
    virtual void setInputPaused(bool paused) = 0;
    // Ends the session; closed() follows, once it has said goodbye or gave up.
    virtual void close() = 0;

signals:
    void event(const speecher::GptLiveEvent &event);
    // The call could not start or broke off; closed() follows.
    void failed(const QString &message);
    void closed();
};

} // namespace speecher
