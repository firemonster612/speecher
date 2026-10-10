#pragma once

#include <QByteArray>

namespace speecher {

// Takes what the speakers play out of the microphone's audio, so a Recording
// on speakers does not hear the other side of a call a second time as the
// user. Both streams are 16 kHz mono s16, as AudioInput delivers them, from
// devices with their own clocks.
class EchoCanceller {
public:
    virtual ~EchoCanceller() = default;
    // System audio, as it arrives.
    virtual void addReference(const QByteArray &pcm) = 0;
    // Microphone audio as it arrives, returned without the system audio. It
    // can come back later than it went in, held until the system audio it
    // echoes has arrived.
    virtual QByteArray process(const QByteArray &microphonePcm) = 0;
    // The microphone audio still held back, for a stop.
    virtual QByteArray flush() = 0;
};

} // namespace speecher
