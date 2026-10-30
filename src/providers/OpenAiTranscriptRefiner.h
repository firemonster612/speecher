#pragma once

#include "providers/OpenAiAuthProvider.h"
#include "providers/TranscriptRefiner.h"

namespace speecher {

class OpenAiRefiner;
class SecretStore;

class OpenAiTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit OpenAiTranscriptRefiner(SecretStore *secretStore, QObject *parent = nullptr);

    std::optional<PreparationJob> createRefreshJob(const RefinementSettings &settings,
                                                   const AccountSettings &accounts) override;
    std::optional<ProviderFailure> prepare(const RefinementSettings &settings,
                                           const AccountSettings &accounts) override;
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementContext &context,
                const RefinementSettings &settings,
                const AccountSettings &accounts) override;
    void cancel() override;

private:
    SecretStore *m_secretStore = nullptr;
    OpenAiRefiner *m_refiner = nullptr;
    OpenAiAuth m_auth;
    // The refresh job resolved m_auth on the worker, renewing what had
    // expired; the next prepare() uses it rather than read the sign-in on
    // this thread.
    bool m_authResolvedByJob = false;
};

} // namespace speecher
