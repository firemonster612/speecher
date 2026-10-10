#pragma once

#include "platform/audio/AudioPcmConverter.h"

#include <QList>

namespace speecher {

// How often a loopback capture reads, and so the longest stretch of silence
// it sends at once.
inline constexpr qint64 kLoopbackPollMs = 100;

// Where a packet falls on a loopback capture's timeline, given the
// timeline's elapsed time now and when the packet played and now on the
// performance counter. The timeline runs on unbiased interrupt time, which
// stops while the machine sleeps, so time that passes on it is time capture
// was awake for. The output places packets on the performance counter, which
// counts sleep too, so a packet is placed by how long before now it played.
qint64 loopbackPacketMs(qint64 elapsedMs, qint64 counterNowMs, qint64 counterPlayedAtMs);

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
    // played, in chunks no longer than a poll.
    QList<QByteArray> silenceUntil(qint64 elapsedMs);

private:
    AudioPcmConverter m_converter;
    QAudioFormat m_format;
    // The stretch of the timeline the stream covers, audio and silence alike.
    qint64 m_samples = 0;
};

} // namespace speecher
