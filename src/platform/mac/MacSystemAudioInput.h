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
// silence, which keeps the stream in step with the clock. A stream that
// stops for anything but the permission or the user, such as when its display
// is unplugged, is opened again on the main display and the timeline carries
// on, as long as LoopbackReopen allows.
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
    // Opens a ScreenCaptureKit stream into m_stream, on the main display.
    struct StreamStart;
    StreamStart startStream();
    // Runs on the main thread after the stream stopped, and again a poll later
    // while a new one refuses.
    void reopen(quint64 generation);
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
