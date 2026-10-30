#pragma once

#include "providers/SpeechTranscriber.h"

#include <QUrl>

namespace speecher {

class ClaudeVoiceClient;

class ClaudeSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    explicit ClaudeSpeechTranscriber(QObject *parent = nullptr);

    bool requiresRefresh(const SpeechSettings &settings, const AccountSettings &accounts) const override;
    std::optional<PreparationJob> createPrepareJob(const SpeechSettings &settings,
                                                     const AccountSettings &accounts) override;
    std::optional<ProviderFailure> prepare(const SpeechSettings &settings, const AccountSettings &accounts) override;
    bool renewsSignIn() const override { return true; }
    bool streamsFinalText(const SpeechSettings &) const override { return true; }
    void startAttempt(quint64 attemptId, const SpeechSettings &settings) override;
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override;
    void finishInput(quint64 attemptId) override;
    void cancelAttempt(quint64 attemptId) override;

private:
    void createClient(quint64 attemptId, const SpeechSettings &settings);
    QUrl voiceUrl(const SpeechSettings &settings) const;

    ClaudeVoiceClient *m_client = nullptr;
    quint64 m_attemptId = 0;
    QString m_accessToken;
};

} // namespace speecher
