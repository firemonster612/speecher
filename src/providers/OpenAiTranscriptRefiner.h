#pragma once

#include "dictation/DictationPorts.h"
#include "providers/OpenAiAuthProvider.h"

namespace speecher {

class OpenAiRefiner;
class SecretStore;

class OpenAiTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit OpenAiTranscriptRefiner(SecretStore *secretStore, QObject *parent = nullptr);

    bool requiresRefresh(const RefinementSettings &settings, const AccountSettings &accounts) const override;
    std::optional<RefinementRefreshJob> createRefreshJob(const RefinementSettings &settings,
                                                         const AccountSettings &accounts) override;
    void refresh(const RefinementSettings &settings, const AccountSettings &accounts) override;
    RefinementPrepareResult prepare(const RefinementSettings &settings, const AccountSettings &accounts) override;
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
    // expired; the next prepare() uses it rather than renew on this thread.
    bool m_authResolvedByJob = false;
};

} // namespace speecher
