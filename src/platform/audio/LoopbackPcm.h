#pragma once

#include "platform/audio/AudioPcmConverter.h"

namespace speecher {

// What a loopback capture of the speakers reads, as 16 kHz mono s16 like the
// microphone, on a timeline that starts with the capture. Loopback delivers
// nothing while nothing plays and drops what it could not buffer, so the
// stretches between audio are filled with silence, which keeps the stream in
// step with the clock and a recording's timeline from stalling.
class LoopbackPcm {
public:
    // The output's mix format, set again when capture moves to a new default
    // output; the stream carries on from where the old one stopped.
    void useFormat(const QAudioFormat &format);
    QByteArray convert(const QByteArray &packet);
    // A packet the output marked silent, whose data is to be ignored.
    QByteArray convertSilent(qsizetype bytes);
    // Silence from the end of the stream so far to elapsedMs after it began,
    // before a packet that played then or for a poll that found nothing
    // played.
    QByteArray silenceUntil(qint64 elapsedMs);

private:
    AudioPcmConverter m_converter;
    QAudioFormat m_format;
    // The stretch of the timeline the stream covers: audio, silence and gaps
    // skipped alike.
    qint64 m_samples = 0;
};

} // namespace speecher
