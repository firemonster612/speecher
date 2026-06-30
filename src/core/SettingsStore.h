#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

#include "core/AppSettings.h"

namespace speecher {

class SettingsStore : public QObject {
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr);

    int previewWords() const;
    void setPreviewWords(int value);

    QString theme() const;
    void setTheme(const QString &value);

    bool pauseMediaDuringTranscription() const;
    void setPauseMediaDuringTranscription(bool value);

    QString speechProvider() const;
    void setSpeechProvider(const QString &value);

    QStringList customVocabulary() const;
    void setCustomVocabulary(const QStringList &value);

    QString audioInputDeviceId() const;
    void setAudioInputDeviceId(const QString &value);
    QString audioCaptureMode() const;
    void setAudioCaptureMode(const QString &value);
    bool audioVadEnabled() const;
    void setAudioVadEnabled(bool value);
    int audioPreRollMs() const;
    void setAudioPreRollMs(int value);
    int audioPostRollMs() const;
    void setAudioPostRollMs(int value);
    int audioReadinessTimeoutMs() const;
    void setAudioReadinessTimeoutMs(int value);
    int audioVadThresholdPercent() const;
    void setAudioVadThresholdPercent(int value);
    AudioCaptureSettings audioCaptureSettings() const;
    void setAudioCaptureSettings(const AudioCaptureSettings &value);

    QList<BindingRule> bindingRules() const;
    bool setBindingRules(const QList<BindingRule> &rules, QString *error = nullptr);

    QString refinementProvider() const;
    void setRefinementProvider(const QString &value);

    QString refinementStyle() const;
    void setRefinementStyle(const QString &value);

    QString openAiModel() const;
    void setOpenAiModel(const QString &value);

    QString openAiAuthMode() const;
    void setOpenAiAuthMode(const QString &value);

    QString openAiEffort() const;
    void setOpenAiEffort(const QString &value);

    QString anthropicModel() const;
    void setAnthropicModel(const QString &value);

    QString anthropicAuthMode() const;
    void setAnthropicAuthMode(const QString &value);

    QString anthropicEffort() const;
    void setAnthropicEffort(const QString &value);

    QString outputMethod() const;
    void setOutputMethod(const QString &value);
    bool ydotoolEnabled() const;
    void setYdotoolEnabled(bool value);
    bool restoreClipboardAfterTyping() const;
    void setRestoreClipboardAfterTyping(bool value);

    QString claudeCredentialsPath() const;
    QString claudeEndpointBase() const;
    QString claudeVoicePath() const;

    QString storedApiKeyFallback() const;
    void setStoredApiKeyFallback(const QString &value);
    void clearStoredApiKeyFallback();

    AppSettings snapshot() const;
    QSettings &raw();

signals:
    void audioCaptureSettingsChanged(const AudioCaptureSettings &settings);

private:
    QVariant value(const QString &key, const QVariant &fallback) const;
    void emitAudioCaptureSettingsChangedIfNeeded(const AudioCaptureSettings &previous);
    QSettings m_settings;
};

} // namespace speecher
