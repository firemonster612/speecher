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
    // label names the endpoint in failure messages.
    explicit EndpointTranscriptRefiner(const QString &label, QObject *parent = nullptr);

    std::optional<ProviderFailure> prepare(const RefinementSettings &settings,
                                           const AccountSettings &accounts) override;
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
