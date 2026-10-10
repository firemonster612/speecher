#pragma once

// Objective-C++ only: what MacSystemAudioInput runs on its capture queue,
// apart from ScreenCaptureKit itself so tests can stand in for it.

#include "platform/audio/LoopbackPcm.h"
#include "platform/audio/LoopbackReopen.h"

#include <QAudioFormat>
#include <QList>
#include <QString>

#include <optional>
#include <utility>

#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

namespace speecher {

// How long a stream start may take before capture stops waiting for
// ScreenCaptureKit, which answers on a queue of its own.
inline constexpr qint64 kStreamReplyTimeoutMs = 10'000;

// Whether a ScreenCaptureKit stream started, or why not: there is no display
// to capture alongside, or what ScreenCaptureKit said, nil when it did not
// answer.
struct StreamStart {
    bool started = false;
    bool noDisplay = false;
    NSError *error = nil;
};

// The timeline and the streams that feed it, one at a time, all on the
// capture queue. Each stream has an id, and what a stream no longer current
// sends is dropped. A stream that stops for anything but the permission or
// the user is replaced on the next poll while the timeline carries on, as
// long as LoopbackReopen allows; only a stream that ran for the whole
// allowance restarts it, so streams that keep stopping soon after they start
// end capture rather than reopening forever. A subclass starts and stops the
// streams and says where audio and failures go.
class SystemAudioCapture {
public:
    explicit SystemAudioCapture(QString permissionDenied);
    virtual ~SystemAudioCapture() = default;
    Q_DISABLE_COPY_MOVE(SystemAudioCapture)

    // Starts the timeline and the first stream, whose answer goes to opened().
    void open();
    // The answer to startStream(streamId).
    void started(quint64 streamId, const StreamStart &start);
    void read(quint64 streamId, CMSampleBufferRef buffer);
    void stopped(quint64 streamId, NSError *error);
    // Fills the time since the last poll with silence if no audio came in it,
    // opens a stream when none is open, and gives up on a start that took too
    // long.
    void poll();
    // Ends capture: nothing is sent after it, and a stream that starts later
    // is stopped.
    void detach();

protected:
    // Starts stream streamId on the main display, which answers with
    // started().
    virtual void startStream(quint64 streamId) = 0;
    virtual void stopStream(quint64 streamId) = 0;
    // The first stream's answer, which start() waits for.
    virtual void opened(const StreamStart &start) = 0;
    virtual void sendAudio(const QList<QByteArray> &chunks) = 0;
    virtual void sendFailure(const QString &message) = 0;
    // The host clock, which a test sets.
    virtual CMTime hostTime() const;

private:
    void openStream();
    // After a stream stopped, or one opening refused, with error: fails for
    // the permission or the user, and otherwise leaves the next poll to open
    // another while LoopbackReopen allows.
    void refused(NSError *error, LoopbackReopen::Refusal refusal);
    void fail(const QString &message);
    void send(const QList<QByteArray> &chunks);
    qint64 elapsedMs() const;

    const QString m_permissionDenied;
    bool m_attached = true;
    bool m_opened = false;
    CMTime m_start = kCMTimeInvalid;
    LoopbackPcm m_pcm;
    QAudioFormat m_format;
    // Whether audio came in since the last poll.
    bool m_heard = false;
    LoopbackReopen m_reopen;
    quint64 m_lastStreamId = 0;
    // The stream audio is read from, starting or running; 0 for none.
    quint64 m_streamId = 0;
    // When it began starting, until it answers.
    std::optional<qint64> m_startingSinceMs;
    qint64 m_runningSinceMs = 0;
};

} // namespace speecher
