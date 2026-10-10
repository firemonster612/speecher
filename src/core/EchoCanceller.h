#pragma once

#include <QByteArray>

namespace speecher {

// Takes what the speakers play out of the microphone's audio, so a Recording
// on speakers does not hear the other side of a call a second time as the
// user. Both streams are 16 kHz mono s16, as AudioInput delivers them, from
// devices with their own clocks. Each must be continuous: the canceller lines
// the streams up by how much of each has arrived, so a microphone gated by
// Skip silence, which drops quiet audio, would throw it off. Use it from one
// thread.
class EchoCanceller {
public:
    virtual ~EchoCanceller() = default;
    // System audio, as it arrives.
    virtual void addReference(const QByteArray &pcm) = 0;
    // Microphone audio as it arrives, returned without the system audio. It
    // can come back later than it went in, held until the system audio it
    // echoes has arrived.
    virtual QByteArray process(const QByteArray &microphonePcm) = 0;
    // The microphone audio still held back, for a stop; all of it has then
    // come back, as long as it went in. Nothing goes in after.
    virtual QByteArray flush() = 0;
};

} // namespace speecher
