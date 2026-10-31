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
    // since is held for the stop to return.
    bool endedDuringStop = false;
    // No provider is ready for the next attempt: the session is starting, or
    // the next provider prepares after one failed.
    bool awaitingProvider = false;
    // Audio went past the most that may be held; what follows is dropped
    // too, so the held audio has no gap.
    bool heldPastTheBudget = false;

    // No open attempt can take audio now: it is held.
    bool holdsAudio() const { return finishingPausedAttempt || endedDuringStop || awaitingProvider; }

    // Audio no open attempt can take yet: heard after a resume while the
    // paused attempt still finishes, while the next provider prepares, or
    // after the last attempt ended during the stop. The next attempt gets
    // it, once, oldest first.
    void hold(const QByteArray &pcm)
    {
        m_heldAudio.append(pcm);
        m_heldBytes += pcm.size();
    }
    QByteArray takeOldestHeld()
    {
        const QByteArray pcm = m_heldAudio.takeFirst();
        m_heldBytes -= pcm.size();
        return pcm;
    }
    void dropHeld()
    {
        m_heldAudio.clear();
        m_heldBytes = 0;
    }
    bool hasHeldAudio() const { return !m_heldAudio.isEmpty(); }
    qsizetype heldBytes() const { return m_heldBytes; }

private:
    QList<QByteArray> m_heldAudio;
    qsizetype m_heldBytes = 0;
};

} // namespace speecher
