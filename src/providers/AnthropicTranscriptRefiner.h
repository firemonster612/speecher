#pragma once

#include "dictation/DictationInterfaces.h"

namespace speecher {

class AnthropicApiRefiner;
class ClaudeCodeSessionRefiner;

class AnthropicTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit AnthropicTranscriptRefiner(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const RefinementSettings &settings) const override;
    std::optional<RefinementRefreshJob> createRefreshJob(const RefinementSettings &settings) override;
    void refresh(const RefinementSettings &settings) override;
    RefinementPrepareResult prepare(const RefinementSettings &settings) override;
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementSettings &settings) override;
    void cancel() override;

private:
    bool usesOauth(const RefinementSettings &settings) const;

    AnthropicApiRefiner *m_apiRefiner = nullptr;
    ClaudeCodeSessionRefiner *m_codeSession = nullptr;
    QString m_accessToken;
};

} // namespace speecher
