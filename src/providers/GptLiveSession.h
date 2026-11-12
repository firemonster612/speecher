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
    // from SessionStarted on.
    virtual void start(const QString &accessToken, const QString &accountId,
                       const GptLiveSessionOptions &options) = 0;
    // 16 kHz mono s16le, as it was heard. The session sends it on at the
    // pace it was spoken, so a file's audio waits here.
    virtual void sendAudio(const QByteArray &pcm16k) = 0;
    // How much of the audio sent waits to go out, in milliseconds.
    virtual qint64 queuedMs() const = 0;
    // Ends the session; closed() follows, once it has said goodbye or gave up.
    virtual void close() = 0;

signals:
    void event(const speecher::GptLiveEvent &event);
    // The call could not start or broke off; closed() follows.
    void failed(const QString &message);
    void closed();
};

} // namespace speecher
