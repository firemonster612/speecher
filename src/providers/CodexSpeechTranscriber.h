#pragma once

#include "dictation/DictationPorts.h"

namespace speecher {

class CodexDictationClient;

class CodexSpeechTranscriber final : public SpeechTranscriber {
    Q_OBJECT

public:
    explicit CodexSpeechTranscriber(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const SpeechSettings &settings) const override;
    std::optional<SpeechPrepareJob> createPrepareJob(const SpeechSettings &settings) override;
    SpeechPrepareResult prepare(const SpeechSettings &settings) override;
    void startAttempt(quint64 attemptId, const SpeechSettings &settings) override;
    void sendAudio(quint64 attemptId, const QByteArray &pcm) override;
    void finishInput(quint64 attemptId) override;
    void cancelAttempt(quint64 attemptId) override;

private:
    CodexDictationClient *m_client = nullptr;
    quint64 m_attemptId = 0;
    QString m_accessToken;
};

} // namespace speecher
