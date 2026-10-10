#pragma once

#include "platform/audio/AudioPcmConverter.h"

namespace speecher {

// What a loopback capture of the speakers reads, as 16 kHz mono s16 like the
// microphone. Loopback delivers nothing while nothing plays, so a read that
// finds no audio asks for the silence since the last of it, which keeps the
// stream in step with the clock and a recording's timeline from stalling.
class LoopbackPcm {
public:
    // The output's mix format, set again when capture moves to a new default
    // output; the stream carries on from where the old one stopped.
    void useFormat(const QAudioFormat &format);
    QByteArray convert(const QByteArray &packet);
    // Silence from the end of the stream so far to elapsedMs after it began.
    QByteArray silenceUntil(qint64 elapsedMs);

private:
    AudioPcmConverter m_converter;
    // Samples delivered, audio and silence alike.
    qint64 m_samples = 0;
};

} // namespace speecher
