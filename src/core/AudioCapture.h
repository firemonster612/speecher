#pragma once

#include "core/AppSettings.h"
#include <QAudioSource>
#include <QIODevice>
#include <QList>
#include <QScopedPointer>
#include <QVector>

#include "dictation/DictationInterfaces.h"

namespace speecher {

class SettingsStore;

class AudioCapture : public AudioInput {
    Q_OBJECT

public:
    explicit AudioCapture(SettingsStore *settings = nullptr, QObject *parent = nullptr);
    static QList<AudioInputDeviceInfo> availableInputDevices();

    bool start(QString *error = nullptr) override;
    void stop() override;
    bool isActive() const override;

signals:
    void firstSampleObserved();

private:
    void handleSettingsChanged(const AudioCaptureSettings &settings);
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
    QByteArray toOutputPcm(const QByteArray &data, QString *error);
    QByteArray encodeOutputSamples(const QVector<float> &samples);
    void processOutputChunk(const QByteArray &pcm, float rms);
    void appendPreRoll(const QByteArray &pcm);
    void flushPreRoll();
    void flushPendingPostRoll();
    bool sourceMatches(const AudioCaptureSettings &settings) const;

    SettingsStore *m_settings = nullptr;
    QScopedPointer<QAudioSource> m_source;
    QIODevice *m_device = nullptr;
    QAudioFormat m_sourceFormat;
    QString m_currentDeviceId;
    QString m_currentDeviceLabel;
    AudioCaptureSettings m_captureSettings;
    QByteArray m_preRollBuffer;
    QByteArray m_pendingPostRoll;
    QVector<float> m_resampleBuffer;
    double m_nextInputPosition = 0.0;
    bool m_captureActive = false;
    bool m_sourceStopping = false;
    bool m_seenFirstSample = false;
    bool m_vadSpeaking = false;
    bool m_conversionFailed = false;
};

} // namespace speecher
