#pragma once

#include "core/AppSettings.h"

#include <QSettings>

namespace speecher {

class SettingsCodecs {
public:
    SettingsCodecs();

    bool setupCompleted() const; void setSetupCompleted(bool value);
    bool launchAtLogin() const; void setLaunchAtLogin(bool value);
    int previewWords() const; void setPreviewWords(int value);
    QString theme() const; void setTheme(const QString &value);
    bool pauseMediaDuringTranscription() const; void setPauseMediaDuringTranscription(bool value);
    bool soundsEnabled() const; void setSoundsEnabled(bool value);
    QString speechProvider() const; void setSpeechProvider(const QString &value);
    QStringList customVocabulary() const; void setCustomVocabulary(const QStringList &value);
    QList<VocabularyEntry> vocabularyEntries() const; void setVocabularyEntries(const QList<VocabularyEntry> &entries);
    void recordVocabularyUsage(const QString &text);
    AudioCaptureSettings audioCaptureSettings() const; void setAudioCaptureSettings(const AudioCaptureSettings &value);
    QList<AppRecognitionRule> appRecognitionRules() const; void setAppRecognitionRules(const QList<AppRecognitionRule> &rules);
    QList<BindingRule> bindingRules() const; bool setBindingRules(const QList<BindingRule> &rules, QString *error = nullptr);
    bool correctionLearningEnabled() const; void setCorrectionLearningEnabled(bool value);
    QList<LearnedCorrection> learnedCorrections() const; void setLearnedCorrections(const QList<LearnedCorrection> &corrections);
    void setLearnedCorrectionEnabled(const QString &id, bool enabled); void removeLearnedCorrection(const QString &id);
    QString refinementProvider() const; void setRefinementProvider(const QString &value);
    QString refinementStyle() const; void setRefinementStyle(const QString &value);
    QString defaultWritingProfile() const; void setDefaultWritingProfile(const QString &value);
    QList<WritingProfileSettings> writingProfileSettings() const; void setWritingProfileSettings(const QList<WritingProfileSettings> &value);
    QList<WritingProfileOverride> writingProfileOverrides() const; void setWritingProfileOverrides(const QList<WritingProfileOverride> &value);
    bool useTargetContext() const; void setUseTargetContext(bool value);
    bool includeScreenshotContext() const; void setIncludeScreenshotContext(bool value);
    QString openAiModel() const; void setOpenAiModel(const QString &value);
    QString openAiAuthMode() const; void setOpenAiAuthMode(const QString &value);
    QString openAiEffort() const; void setOpenAiEffort(const QString &value);
    bool openAiFastMode() const; void setOpenAiFastMode(bool value);
    QString openAiCliproxyAccount() const; void setOpenAiCliproxyAccount(const QString &value);
    QString anthropicModel() const; void setAnthropicModel(const QString &value);
    QString anthropicAuthMode() const; void setAnthropicAuthMode(const QString &value);
    QString anthropicEffort() const; void setAnthropicEffort(const QString &value);
    bool anthropicFastMode() const; void setAnthropicFastMode(bool value);
    QString anthropicCliproxyAccount() const; void setAnthropicCliproxyAccount(const QString &value);
    QString cliproxyOauthDir() const;
    QString cliproxyBaseUrl() const; void setCliproxyBaseUrl(const QString &value);
    QString cliproxyApiKey() const; void setCliproxyApiKey(const QString &value);
    QString outputMethod() const; void setOutputMethod(const QString &value);
    OutputFormat outputFormat() const; void setOutputFormat(OutputFormat value);
    bool ydotoolEnabled() const; void setYdotoolEnabled(bool value);
    bool restoreClipboardAfterTyping() const; void setRestoreClipboardAfterTyping(bool value);
    int completionStatusDurationMs() const; void setCompletionStatusDurationMs(int value);
    QList<PasteRule> pasteRules() const; void setPasteRules(const QList<PasteRule> &rules);
    UpdateChannel updateChannel() const; void setUpdateChannel(UpdateChannel value);
    bool autoCheckUpdates() const; void setAutoCheckUpdates(bool value);
    bool autoInstallUpdates() const; void setAutoInstallUpdates(bool value);
    QString claudeCredentialsPath() const; QString claudeEndpointBase() const; QString claudeVoicePath() const;
    QString storedApiKeyFallback() const; void setStoredApiKeyFallback(const QString &value); void clearStoredApiKeyFallback();
    AppSettings snapshot() const;

private:
    QVariant value(const QString &key, const QVariant &fallback) const;

protected:
    QSettings m_settings;
};

} // namespace speecher
