#pragma once

#include <QByteArray>
#include <QList>

namespace speecher {

// Where a Dictation Session's speech stands between its attempts, and the
// audio that waits for the next one.
struct BetweenAttempts {
    // The current attempt is finishing because of a pause. Its end is not a
    // rollover, and the next attempt may only open once it has come.
    bool finishingPausedAttempt = false;
    // The last attempt ended while the microphone was stopping; the post-roll
    // since waits in pendingAudio for the stop to return.
    bool endedDuringStop = false;
    // No provider is ready for the next attempt: the session is starting, or
    // the next provider prepares after one failed.
    bool awaitingProvider = false;
    // Audio no open attempt can take yet: heard after a resume while the
    // paused attempt still finishes, while the next provider prepares, or
    // after the last attempt ended during the stop. The next attempt gets
    // it, once.
    QList<QByteArray> pendingAudio;

    // No open attempt can take audio now: it waits in pendingAudio.
    bool holdsAudio() const { return finishingPausedAttempt || endedDuringStop || awaitingProvider; }
};

} // namespace speecher
