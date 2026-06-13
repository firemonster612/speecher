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
    QString openAiModel;
    QString openAiAuthMode = QStringLiteral("auto");
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
