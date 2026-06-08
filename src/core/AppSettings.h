#pragma once

#include <QString>
#include <QStringList>

namespace speecher {

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

struct RefinementSettings {
    QString providerId = QStringLiteral("openai");
    QString style = QStringLiteral("balanced");
    QString openAiModel;
    QString openAiAuthMode = QStringLiteral("auto");
};

struct OutputSettings {
    QString method = QStringLiteral("automatic");
    bool ydotoolEnabled = false;
};

struct AppSettings {
    UiSettings ui;
    SpeechSettings speech;
    RefinementSettings refinement;
    OutputSettings output;
};

} // namespace speecher
