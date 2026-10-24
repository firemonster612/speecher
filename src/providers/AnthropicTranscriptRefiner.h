#pragma once

#include "dictation/DictationPorts.h"

namespace speecher {

class AnthropicApiRefiner;

// The access token the Anthropic sign-in mode points at: Claude Code's
// credentials, a CLI Proxy API account file, or the CLI Proxy API server's
// key. The message says where it came from, or why there is none. With
// refreshExpired an expired token is renewed over the network first, so
// only a worker thread may ask for that. accessToken may be null.
RefinementPrepareResult loadAnthropicAccessToken(const RefinementSettings &settings,
                                                 QString *accessToken,
                                                 bool refreshExpired);
// Whether that token has expired and a refresh could renew it.
bool anthropicSignInNeedsRefresh(const RefinementSettings &settings);

class AnthropicTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit AnthropicTranscriptRefiner(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const RefinementSettings &settings) const override;
    bool supportsScreenshotContext(const RefinementSettings &settings) const override;
    std::optional<RefinementRefreshJob> createRefreshJob(const RefinementSettings &settings) override;
    void refresh(const RefinementSettings &settings) override;
    RefinementPrepareResult prepare(const RefinementSettings &settings) override;
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementContext &context,
                const RefinementSettings &settings) override;
    void cancel() override;

private:
    AnthropicApiRefiner *m_apiRefiner = nullptr;
    QString m_accessToken;
};

} // namespace speecher
