#pragma once

#include "core/settings/SettingsCodecs.h"

#include <QObject>
#include <QSettings>

#include <functional>

namespace speecher {

class SettingsStore : public QObject, private SettingsCodecs {
    Q_OBJECT

public:
    using LaunchAtLoginReconciler = std::function<bool(bool, QString *)>;

    explicit SettingsStore(QObject *parent = nullptr);
    void applySnapshot(const AppSettings &draft);
    bool launchAtLogin() const;
    void setLaunchAtLogin(bool enabled);
    void setLaunchAtLoginReconciler(LaunchAtLoginReconciler reconcile);
    void reconcileLaunchAtLogin();
    void setCorrectionLearningEnabled(bool enabled);
    bool recordCorrectionEvidence(const CorrectionEvidence &evidence,
                                  const QString &applicationId);

    using SettingsCodecs::anthropicAuthMode;
    using SettingsCodecs::autoCheckUpdates;
    using SettingsCodecs::autoInstallUpdates;
    using SettingsCodecs::anthropicCliproxyAccount;
    using SettingsCodecs::anthropicEffort;
    using SettingsCodecs::anthropicFastMode;
    using SettingsCodecs::anthropicModel;
    using SettingsCodecs::cliproxyOauthDir;
    using SettingsCodecs::cliproxyBaseUrl;
    using SettingsCodecs::setCliproxyBaseUrl;
    using SettingsCodecs::cliproxyApiKey;
    using SettingsCodecs::setCliproxyApiKey;
    using SettingsCodecs::appRecognitionRules;
    using SettingsCodecs::audioCaptureSettings;
    using SettingsCodecs::bindingRules;
    using SettingsCodecs::claudeCredentialsPath;
    using SettingsCodecs::claudeEndpointBase;
    using SettingsCodecs::claudeVoicePath;
    using SettingsCodecs::clearStoredApiKeyFallback;
    using SettingsCodecs::completionStatusDurationMs;
    using SettingsCodecs::correctionLearningEnabled;
    using SettingsCodecs::customVocabulary;
    using SettingsCodecs::defaultWritingProfile;
    using SettingsCodecs::includeScreenshotContext;
    using SettingsCodecs::learnedCorrections;
    using SettingsCodecs::openAiAuthMode;
    using SettingsCodecs::openAiCliproxyAccount;
    using SettingsCodecs::openAiEffort;
    using SettingsCodecs::openAiFastMode;
    using SettingsCodecs::openAiModel;
    using SettingsCodecs::outputFormat;
    using SettingsCodecs::outputMethod;
    using SettingsCodecs::pasteRules;
    using SettingsCodecs::pauseMediaDuringTranscription;
    using SettingsCodecs::previewWords;
    using SettingsCodecs::recordVocabularyUsage;
    using SettingsCodecs::refinementProvider;
    using SettingsCodecs::refinementStyle;
    using SettingsCodecs::removeLearnedCorrection;
    using SettingsCodecs::restoreClipboardAfterTyping;
    using SettingsCodecs::setAnthropicAuthMode;
#ifndef Q_OS_MACOS
    using SettingsCodecs::setAutoCheckUpdates;
    using SettingsCodecs::setAutoInstallUpdates;
#endif
    using SettingsCodecs::setAnthropicCliproxyAccount;
    using SettingsCodecs::setAnthropicEffort;
    using SettingsCodecs::setAnthropicFastMode;
    using SettingsCodecs::setAnthropicModel;
    using SettingsCodecs::setAppRecognitionRules;
    using SettingsCodecs::setBindingRules;
    using SettingsCodecs::setCompletionStatusDurationMs;
    using SettingsCodecs::setCustomVocabulary;
    using SettingsCodecs::setDefaultWritingProfile;
    using SettingsCodecs::setIncludeScreenshotContext;
    using SettingsCodecs::setLearnedCorrectionEnabled;
    using SettingsCodecs::setLearnedCorrections;
    using SettingsCodecs::setOpenAiAuthMode;
    using SettingsCodecs::setOpenAiCliproxyAccount;
    using SettingsCodecs::setOpenAiEffort;
    using SettingsCodecs::setOpenAiFastMode;
    using SettingsCodecs::setOpenAiModel;
    using SettingsCodecs::setOutputFormat;
    using SettingsCodecs::setOutputMethod;
    using SettingsCodecs::setPasteRules;
    using SettingsCodecs::setPauseMediaDuringTranscription;
    using SettingsCodecs::setPreviewWords;
    using SettingsCodecs::setRefinementProvider;
    using SettingsCodecs::setRefinementStyle;
    using SettingsCodecs::setRestoreClipboardAfterTyping;
    using SettingsCodecs::setSoundsEnabled;
    using SettingsCodecs::setSpeechProvider;
    using SettingsCodecs::setSetupCompleted;
    using SettingsCodecs::setStoredApiKeyFallback;
    using SettingsCodecs::setTheme;
    using SettingsCodecs::setUseTargetContext;
    using SettingsCodecs::setVocabularyEntries;
    using SettingsCodecs::setWritingProfileOverrides;
    using SettingsCodecs::setWritingProfileSettings;
    using SettingsCodecs::setYdotoolEnabled;
#ifndef Q_OS_MACOS
    using SettingsCodecs::setUpdateChannel;
#endif
    using SettingsCodecs::snapshot;
    using SettingsCodecs::soundsEnabled;
    using SettingsCodecs::speechProvider;
    using SettingsCodecs::setupCompleted;
    using SettingsCodecs::storedApiKeyFallback;
    using SettingsCodecs::theme;
    using SettingsCodecs::useTargetContext;
    using SettingsCodecs::vocabularyEntries;
    using SettingsCodecs::writingProfileOverrides;
    using SettingsCodecs::writingProfileSettings;
    using SettingsCodecs::ydotoolEnabled;
    using SettingsCodecs::updateChannel;

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
    void setAudioCaptureSettings(const AudioCaptureSettings &value);
#ifdef Q_OS_MACOS
    void setUpdateChannel(UpdateChannel value);
    void setAutoCheckUpdates(bool value);
    void setAutoInstallUpdates(bool value);
#endif

    QSettings &raw();

signals:
    void audioCaptureSettingsChanged(const AudioCaptureSettings &settings);
    void correctionLearningEnabledChanged(bool enabled);
#ifdef Q_OS_MACOS
    void updateSettingsChanged();
#endif

private:
    void emitAudioCaptureSettingsChangedIfNeeded(const AudioCaptureSettings &previous);

    LaunchAtLoginReconciler m_reconcileLaunchAtLogin;
};

} // namespace speecher
