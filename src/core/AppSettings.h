#pragma once

#include "core/OutputFormat.h"
#include "core/PasteRules.h"

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
    QString openAiModel = QStringLiteral("gpt-5.6-luna");
    QString openAiAuthMode = QStringLiteral("auto");
    QString openAiEffort = QStringLiteral("none");
    QString anthropicModel = QStringLiteral("claude-sonnet-4-6");
    QString anthropicAuthMode = QStringLiteral("claude_code");
    QString anthropicEffort = QStringLiteral("low");
    QString anthropicEndpointBase = QStringLiteral("https://api.anthropic.com/v1");
    QString claudeCredentialsPath;
    QStringList bindingVocabulary;
    QString defaultWritingProfile = QStringLiteral("general");
    bool useTargetContext = true;
    bool includeScreenshotContext = false;
};

struct OutputSettings {
    QString method = QStringLiteral("automatic");
    OutputFormat format = OutputFormat::PlainText;
    bool ydotoolEnabled = false;
    bool restoreClipboardAfterTyping = false;
    QList<PasteRule> pasteRules = defaultPasteRules();
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
