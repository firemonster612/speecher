#pragma once

#include "core/OutputFormat.h"
#include "core/PasteRules.h"
#include "core/LearnedCorrection.h"

#include <QString>
#include <QStringList>
#include <QList>

namespace speecher {

enum class UpdateChannel {
    Stable,
    Nightly,
};

inline QString updateChannelName(UpdateChannel channel)
{
    return channel == UpdateChannel::Nightly ? QStringLiteral("nightly")
                                             : QStringLiteral("stable");
}

inline UpdateChannel updateChannelFromName(const QString &name)
{
    return name == QStringLiteral("nightly") ? UpdateChannel::Nightly
                                             : UpdateChannel::Stable;
}

struct BindingRule {
    QString phrase;
    QString replacement;

    bool operator==(const BindingRule &other) const = default;
};

struct VocabularyEntry {
    QString term;
    QString source = QStringLiteral("manual");
    bool starred = false;
    int frequency = 0;
    qint64 lastUsedMs = 0;

    bool operator==(const VocabularyEntry &other) const = default;
};

struct UiSettings {
    int previewWords = 7;
    QString theme = QStringLiteral("system");
    bool pauseMediaDuringTranscription = true;
    bool soundsEnabled = false;
};

struct SpeechSettings {
    QString providerId = QStringLiteral("claude");
    QString claudeAuthMode = QStringLiteral("oauth");
    QString codexAuthMode = QStringLiteral("auto");
    QString language = QStringLiteral("en");
    QStringList vocabulary;
    QString claudeCredentialsPath;
    QString claudeEndpointBase;
    QString claudeVoicePath;
    QString cliproxyOauthDir;
    QString claudeCliproxyAccount;
    QString codexCliproxyAccount;
};

struct AudioCaptureSettings {
    QString deviceId;
    QString mode = QStringLiteral("on_demand");
    bool vadEnabled = false;
    int preRollMs = 250;
    int postRollMs = 200;
    int readinessTimeoutMs = 900;
    int vadThresholdPercent = 2;

    bool operator==(const AudioCaptureSettings &other) const = default;
};

struct RefinementSettings {
    QString providerId = QStringLiteral("openai");
    QString style = QStringLiteral("balanced");
    QString openAiModel = QStringLiteral("gpt-5.6-luna");
    QString openAiAuthMode = QStringLiteral("auto");
    QString openAiEffort = QStringLiteral("none");
    bool openAiFastMode = true;
    QString openAiCliproxyAccount;
    QString anthropicModel = QStringLiteral("claude-sonnet-4-6");
    QString anthropicAuthMode = QStringLiteral("oauth");
    QString anthropicEffort = QStringLiteral("low");
    bool anthropicFastMode = true;
    QString anthropicCliproxyAccount;
    QString cliproxyOauthDir;
    QString cliproxyBaseUrl;
    QString cliproxyApiKey;
    QString anthropicEndpointBase = QStringLiteral("https://api.anthropic.com/v1");
    QString claudeCredentialsPath;
    QStringList bindingVocabulary;
    QString defaultWritingProfile = QStringLiteral("other");
    QList<WritingProfileSettings> writingProfiles = defaultWritingProfileSettings();
    QList<WritingProfileOverride> writingProfileOverrides;
    QString tone = QStringLiteral("none");
    bool useTargetContext = true;
    bool includeScreenshotContext = false;
};

struct OutputSettings {
    QString method = QStringLiteral("automatic");
    OutputFormat format = OutputFormat::PlainText;
    bool ydotoolEnabled = false;
    bool restoreClipboardAfterTyping = false;
    int completionStatusDurationMs = 500;
    QList<PasteRule> pasteRules = defaultPasteRules();
};

struct UpdateSettings {
    UpdateChannel channel = UpdateChannel::Stable;
    bool autoCheck = true;
    bool autoInstall = false;
};

struct AppSettings {
    bool setupCompleted = false;
#ifdef Q_OS_MACOS
    bool launchAtLogin = true;
#else
    bool launchAtLogin = false;
#endif
    UiSettings ui;
    SpeechSettings speech;
    AudioCaptureSettings audio;
    QList<AppRecognitionRule> appRecognitionRules;
    RefinementSettings refinement;
    OutputSettings output;
    UpdateSettings updates;
    QList<BindingRule> bindings;
    QList<VocabularyEntry> vocabulary;
    QList<LearnedCorrection> learnedCorrections;
    bool correctionLearningEnabled = true;
};

} // namespace speecher
