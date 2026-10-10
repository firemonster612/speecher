#pragma once

#include "core/AppSettings.h"
#include "dictation/DictationPorts.h"
#include "platform/audio/AudioPcmConverter.h"

#include <QAudioFormat>
#include <QByteArrayView>
#include <QList>
#include <QMutex>

#include <functional>
#include <memory>

namespace speecher {

// A microphone capture that hands over its audio from a thread of its own,
// CoreAudio's I/O thread, from a successful open until its destruction.
class MacMicrophoneCapture {
public:
    using Deliver = std::function<void(QByteArrayView pcm)>;
    using Fail = std::function<void(const QString &message)>;

    virtual ~MacMicrophoneCapture() = default;
    // Why it could not open, or nothing. Once open it delivers audio in
    // format() and reports through fail why it stopped.
    virtual QString open(Deliver deliver, Fail fail) = 0;
    virtual QAudioFormat format() const = 0;
    // Why it records without voice processing, once open.
    virtual QString echoCancellationWarning() const { return {}; }
};

// The recording's microphone on macOS: a MacMicrophoneCapture, made afresh for
// each start, delivered on the input's thread as 16 kHz mono s16. A stop keeps
// capturing for postRollMs and delivers all of it before it returns, as
// QtAudioInput's does.
class MacMicrophoneInput : public AudioInput {
public:
    using CaptureFactory = std::function<std::unique_ptr<MacMicrophoneCapture>()>;

    MacMicrophoneInput(CaptureFactory createCapture, int postRollMs, QObject *parent = nullptr);
    ~MacMicrophoneInput() override;

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;
    QString echoCancellationWarning() const override;

private:
    // Run on the capture's thread.
    void queueAudio(QByteArrayView pcm);
    // Run on the input's thread.
    void deliverAudio();
    void endCapture();
    void fail(quint64 generation, const QString &message);

    CaptureFactory m_createCapture;
    int m_postRollMs;
    std::unique_ptr<MacMicrophoneCapture> m_capture;
    AudioPcmConverter m_converter;
    QString m_echoCancellationWarning;
    // Bumped by every stop, so a failure the capture posted before it is
    // dropped rather than reaching the next start, and a delivery a stop
    // interrupted ends there.
    quint64 m_generation = 0;
    // Guards the audio the capture hands the input's thread.
    QMutex m_mutex;
    QList<QByteArray> m_queued;
};

// The recording's microphone with macOS' voice processing, which cancels the
// echo of what the speakers play, on the microphone settings select. Without
// voice processing, when it cannot start, with why as its warning.
std::unique_ptr<MacMicrophoneCapture> createVoiceProcessingCapture(const AudioCaptureSettings &settings);

} // namespace speecher
