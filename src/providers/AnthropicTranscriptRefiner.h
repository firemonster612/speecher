#pragma once

#include "dictation/DictationPorts.h"

namespace speecher {

class AnthropicApiRefiner;

class AnthropicTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit AnthropicTranscriptRefiner(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const RefinementSettings &settings, const AccountSettings &accounts) const override;
    bool supportsScreenshotContext(const RefinementSettings &settings) const override;
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
    AnthropicApiRefiner *m_apiRefiner = nullptr;
    QString m_accessToken;
    // The refresh job resolved m_accessToken on the worker, renewing what had
    // expired; the next prepare() uses it rather than renew on this thread.
    bool m_authResolvedByJob = false;
};

} // namespace speecher
