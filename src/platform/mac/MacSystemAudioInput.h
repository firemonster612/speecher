#pragma once

#include "dictation/DictationPorts.h"

#include <QAudioFormat>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CMSampleBuffer.h>

#include <memory>

namespace speecher {

class SystemAudioReader;

// The samples of a ScreenCaptureKit audio buffer, interleaved, and their
// layout; an invalid format when the converter cannot read them.
struct SampleBufferPcm {
    QAudioFormat format;
    QByteArray data;
};
SampleBufferPcm pcmForSampleBuffer(CMSampleBufferRef buffer);

// Whether a ScreenCaptureKit error means Speecher may not record the screen
// and system audio, at start or because the permission was taken away.
bool isScreenRecordingDenied(CFErrorRef error);

// What the speakers play, captured with ScreenCaptureKit as 16 kHz mono s16
// like the microphone, without Speecher's own sounds. ScreenCaptureKit
// captures audio only alongside a display, so the stream also takes the
// smallest, slowest video it allows and drops it. It delivers nothing while
// nothing plays, so a poll on the capture queue fills that stretch with
// silence, which keeps the stream in step with the clock.
class MacSystemAudioInput final : public AudioInput {
public:
    // permissionDenied is what start() and failed say when Screen & System
    // Audio Recording is off for Speecher, worded in core.
    explicit MacSystemAudioInput(QString permissionDenied, QObject *parent = nullptr);
    ~MacSystemAudioInput() override;

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

private:
    friend class SystemAudioReader;

    // Leaves whatever it got as far as for stop() to tear down when it fails.
    QString open();
    // Runs on the main thread.
    void fail(quint64 generation, const QString &message);

    // The ScreenCaptureKit stream and its queue, kept opaque so this header
    // stays C++.
    struct Stream;
    std::unique_ptr<Stream> m_stream;
    QString m_permissionDenied;
    // Bumped by every stop, so audio and failures the capture queue posted
    // before it are dropped rather than reaching the next start.
    quint64 m_generation = 0;
};

} // namespace speecher
