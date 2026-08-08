#pragma once

#include "dictation/DictationInterfaces.h"

namespace speecher {

class AnthropicApiRefiner;

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
