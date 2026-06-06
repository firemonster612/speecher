#pragma once

#include "dictation/DictationInterfaces.h"
#include "providers/OpenAiAuthProvider.h"

namespace speecher {

class OpenAiRefiner;
class SecretStore;

class OpenAiTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit OpenAiTranscriptRefiner(SecretStore *secretStore, QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const RefinementSettings &settings) const override;
    void refresh(const RefinementSettings &settings) override;
    RefinementPrepareResult prepare(const RefinementSettings &settings) override;
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementSettings &settings) override;
    void cancel() override;

private:
    SecretStore *m_secretStore = nullptr;
    OpenAiRefiner *m_refiner = nullptr;
    OpenAiAuth m_auth;
};

} // namespace speecher
