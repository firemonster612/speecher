#pragma once

#include "dictation/DictationPorts.h"

namespace speecher {

class AnthropicApiRefiner;
class ChatCompletionsRefiner;

// Refinement through the refinement Custom Endpoint, in the OpenAI Chat
// Completions or Anthropic Messages format.
class EndpointTranscriptRefiner final : public TranscriptRefiner {
    Q_OBJECT

public:
    explicit EndpointTranscriptRefiner(QObject *parent = nullptr);

    QString id() const override;
    QString label() const override;
    bool requiresRefresh(const RefinementSettings &settings, const AccountSettings &accounts) const override;
    void refresh(const RefinementSettings &settings, const AccountSettings &accounts) override;
    RefinementPrepareResult prepare(const RefinementSettings &settings, const AccountSettings &accounts) override;
    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const RefinementContext &context,
                const RefinementSettings &settings,
                const AccountSettings &accounts) override;
    void cancel() override;

private:
    ChatCompletionsRefiner *m_chat = nullptr;
    AnthropicApiRefiner *m_messages = nullptr;
};

} // namespace speecher
