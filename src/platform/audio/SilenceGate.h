#pragma once

#include <QByteArray>
#include <QList>

namespace speecher {

// Skip silence for one capture: quiet audio is held back, and only speech is
// sent on, led in by up to the pre-roll of the quiet before it and trailed by
// up to the post-roll after it, so the edges of words survive. Sizes are in
// bytes of PCM.
class SilenceGate {
public:
    void setSizes(int preRollBytes, int postRollBytes);

    // Keeps pcm as the newest quiet audio, up to the pre-roll in all.
    void holdQuiet(const QByteArray &pcm);
    // Takes one chunk and returns the audio to send on, in order.
    QList<QByteArray> pass(const QByteArray &pcm, bool voiced);
    // Hands over the held quiet audio.
    QByteArray takePreRoll();
    // Hands over the quiet since speech last stopped, up to the post-roll, for
    // a capture that ends before it fills.
    QByteArray flushPostRoll();
    // Starts over for a new capture, keeping the held quiet when asked.
    void reset(bool keepPreRoll);

private:
    int m_preRollBytes = 0;
    int m_postRollBytes = 0;
    // The newest quiet audio, to lead into the next speech.
    QByteArray m_preRoll;
    // Quiet audio since speech last stopped, sent on if speech resumes before
    // it fills the post-roll.
    QByteArray m_pendingPostRoll;
    bool m_speaking = false;
};

} // namespace speecher
