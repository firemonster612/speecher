#pragma once

#include <QByteArray>
#include <QList>

namespace speecher {

// Skip silence for one capture: quiet audio is held back, and only speech is
// sent on, led in by up to the pre-roll of the quiet before it and trailed by
// up to the post-roll after it, so the edges of words survive. Sizes are in
// bytes of PCM.
struct SilenceGate {
    // The newest quiet audio, to lead into the next speech.
    QByteArray preRoll;
    // Quiet audio since speech last stopped, sent on if speech resumes before
    // it fills the post-roll.
    QByteArray pendingPostRoll;
    bool speaking = false;

    // Keeps pcm as the newest quiet audio, up to preRollBytes in all.
    void holdQuiet(const QByteArray &pcm, int preRollBytes);
    // Takes one chunk and returns the audio to send on, in order.
    QList<QByteArray> pass(const QByteArray &pcm, bool voiced, int preRollBytes, int postRollBytes);
};

} // namespace speecher
