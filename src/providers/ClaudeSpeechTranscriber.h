#pragma once

#include "dictation/DictationInterfaces.h"

#include <QUrl>

namespace speecher {

class ClaudeVoiceClient;

class ClaudeSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    explicit ClaudeSpeechTranscriber(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const SpeechSettings &settings) const override;
    std::optional<SpeechPrepareJob> createPrepareJob(const SpeechSettings &settings) override;
    SpeechPrepareResult prepare(const SpeechSettings &settings) override;
    void start(const SpeechSettings &settings) override;
    void sendAudio(const QByteArray &pcm) override;
    void stop() override;

private:
    QUrl voiceUrl(const SpeechSettings &settings) const;

    ClaudeVoiceClient *m_client = nullptr;
    QString m_accessToken;
};

} // namespace speecher
