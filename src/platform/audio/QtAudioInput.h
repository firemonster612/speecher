#pragma once

#include "core/AppSettings.h"
#include <QAudioSource>
#include <QIODevice>
#include <QList>
#include <QMediaDevices>
#include <QScopedPointer>
#include <QTimer>
#include "platform/audio/AudioPcmConverter.h"
#include "platform/audio/SilenceGate.h"

#include "dictation/DictationPorts.h"

namespace speecher {

// Why a microphone capture cannot start when the system has no microphone.
QString noMicrophoneText();

class QtAudioInput : public AudioInput {
    Q_OBJECT

public:
    explicit QtAudioInput(const AudioCaptureSettings &settings = {}, QObject *parent = nullptr);
    static QList<AudioInputDeviceInfo> availableInputDevices();

    void warmUp() override;
    void useDevice(const QString &deviceId) override;
    void start() override;
    void stop() override;
    void abort() override;
    bool isActive() const override;
    void clearPreRoll() override;
    void keepSilence() override;

public slots:
    void applySettings(const AudioCaptureSettings &settings);

private:
    // Where the capture is. Starting waits for the source's first audio, up
    // to the readiness timeout; Stopping takes the post-roll, on a timer.
    enum class Capture { Off, Starting, On, Stopping };

    void ensureMediaDevicesInitialized();
    void handleAudioInputsChanged();
    void syncWarmSource();
    AudioCaptureSettings currentSettings() const;
    bool ensureSourceRunning(const AudioCaptureSettings &settings, QString *error);
    void captureStarted();
    void readinessTimedOut();
    void finishStop();
    void reportStopped();
    // Off at once, with no signal: the source stays open only when warm.
    void endCapture();
    void stopSource();
    void onReadyRead();
    void handleSourceStateChanged(QAudio::State state);
    void failCapture(const QString &message);
    void processOutputChunk(const QByteArray &pcm, float rms);
    void appendPreRoll(const QByteArray &pcm);
    void flushPreRoll();
    void flushPendingPostRoll();
    bool sourceMatches(const AudioCaptureSettings &settings) const;

    QMediaDevices m_mediaDevices;
    QScopedPointer<QAudioSource> m_source;
    QIODevice *m_device = nullptr;
    AudioPcmConverter m_converter;
    QString m_currentDeviceId;
    QString m_currentDeviceLabel;
    AudioCaptureSettings m_captureSettings;
    SilenceGate m_gate;
    Capture m_capture = Capture::Off;
    // Moves on as a capture starts and as it ends, so code that emitted can
    // tell a handler ended the capture, or started another.
    quint64 m_captureId = 0;
    QTimer m_readinessTimer;
    QTimer m_postRollTimer;
    bool m_sourceStopping = false;
    bool m_seenFirstSample = false;
    bool m_conversionFailed = false;
    bool m_mediaDevicesInitialized = false;
    bool m_keepsSilence = false;
};

} // namespace speecher
