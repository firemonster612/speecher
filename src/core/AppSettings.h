#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace speecher {

struct BindingRule {
    QString phrase;
    QString replacement;

    bool operator==(const BindingRule &other) const = default;
};

struct UiSettings {
    int previewWords = 8;
    QString theme = QStringLiteral("system");
    bool pauseMediaDuringTranscription = true;
};

struct SpeechSettings {
    QString providerId = QStringLiteral("claude");
    QStringList vocabulary;
    QString claudeCredentialsPath;
    QString claudeEndpointBase;
    QString claudeVoicePath;
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
    QString openAiModel = QStringLiteral("gpt-5.5");
    QString openAiAuthMode = QStringLiteral("auto");
    QString openAiEffort = QStringLiteral("none");
    QString anthropicModel = QStringLiteral("claude-sonnet-4-6");
    QString anthropicAuthMode = QStringLiteral("claude_code");
    QString anthropicEffort = QStringLiteral("low");
    QString anthropicEndpointBase = QStringLiteral("https://api.anthropic.com/v1");
    QString claudeCredentialsPath;
    QStringList bindingVocabulary;
};

struct OutputSettings {
    QString method = QStringLiteral("automatic");
    bool ydotoolEnabled = false;
    bool restoreClipboardAfterTyping = false;
};

struct AppSettings {
    UiSettings ui;
    SpeechSettings speech;
    AudioCaptureSettings audio;
    RefinementSettings refinement;
    OutputSettings output;
    QList<BindingRule> bindings;
};

} // namespace speecher
