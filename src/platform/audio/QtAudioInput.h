#pragma once

#include "core/AppSettings.h"
#include <QAudioSource>
#include <QIODevice>
#include <QList>
#include <QMediaDevices>
#include <QScopedPointer>
#include "platform/audio/AudioPcmConverter.h"

#include "dictation/DictationInterfaces.h"

namespace speecher {

class QtAudioInput : public AudioInput {
    Q_OBJECT

public:
    explicit QtAudioInput(const AudioCaptureSettings &settings = {}, QObject *parent = nullptr);
    static QList<AudioInputDeviceInfo> availableInputDevices();

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

public slots:
    void applySettings(const AudioCaptureSettings &settings);

signals:
    void firstSampleObserved();

private:
    void handleAudioInputsChanged();
    void syncWarmSource();
    AudioCaptureSettings currentSettings() const;
    bool ensureSourceRunning(const AudioCaptureSettings &settings, QString *error);
    bool waitForFirstSample(const AudioCaptureSettings &settings, QString *error);
    void waitForPostRoll(const AudioCaptureSettings &settings);
    void stopSource();
    void resetCaptureGate(bool keepPreRoll);
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
    QByteArray m_preRollBuffer;
    QByteArray m_pendingPostRoll;
    bool m_captureActive = false;
    bool m_sourceStopping = false;
    bool m_seenFirstSample = false;
    bool m_vadSpeaking = false;
    bool m_conversionFailed = false;
};

} // namespace speecher
