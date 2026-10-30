#include "providers/EndpointTranscriptRefiner.h"

#include "providers/AnthropicApiRefiner.h"
#include "providers/ChatCompletionsRefiner.h"
#include "providers/CustomEndpoints.h"
#include "providers/ProviderFailureClassification.h"

namespace speecher {

EndpointTranscriptRefiner::EndpointTranscriptRefiner(const QString &label, QObject *parent)
    : TranscriptRefiner(parent)
    , m_chat(new ChatCompletionsRefiner(label, ChatCompletionsRefiner::Audience::Server, this))
    , m_messages(new AnthropicApiRefiner(this, 60000, 120000, label))
{
    connect(m_chat, &ChatCompletionsRefiner::delta, this, &TranscriptRefiner::delta);
    connect(m_chat, &ChatCompletionsRefiner::completed, this, &TranscriptRefiner::completed);
    connect(m_chat, &ChatCompletionsRefiner::failed, this, &TranscriptRefiner::failed);
    connect(m_messages, &AnthropicApiRefiner::delta, this, &TranscriptRefiner::delta);
    connect(m_messages, &AnthropicApiRefiner::completed, this, &TranscriptRefiner::completed);
    connect(m_messages, &AnthropicApiRefiner::failed, this, [this](const ProviderFailure &failure) {
        emit failed(selfHostedFailure(failure));
    });
}

std::optional<ProviderFailure> EndpointTranscriptRefiner::prepare(const RefinementSettings &settings,
                                                                  const AccountSettings &)
{
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings);
    if (endpoint.apiBase.isEmpty()) {
        return ProviderFailure{ProviderFailureKind::Unavailable,
                               settings.endpoint.preset == QStringLiteral("cliproxy")
                                   ? QStringLiteral("Set the CLI Proxy API server URL for the custom endpoint.")
                                   : QStringLiteral("Set the custom endpoint's server URL.")};
    }
    if (endpoint.model.isEmpty()) {
        return ProviderFailure{ProviderFailureKind::Unavailable,
                               QStringLiteral("Choose a model for the custom endpoint.")};
    }
    return std::nullopt;
}

void EndpointTranscriptRefiner::refine(const QString &rawTranscript,
                                       const QStringList &vocabulary,
                                       const RefinementContext &context,
                                       const RefinementSettings &settings,
                                       const AccountSettings &accounts)
{
    if (const std::optional<ProviderFailure> failure = prepare(settings, accounts)) {
        emit failed(*failure);
        return;
    }
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings);
    if (endpoint.format == QStringLiteral("anthropic")) {
        m_messages->refineWithApiKey(rawTranscript, vocabulary,
                                     endpoint.apiKey, endpoint.apiBase, endpoint.model,
                                     settings.style, context);
        return;
    }
    m_chat->refine(rawTranscript, vocabulary, endpoint.apiKey,
                   endpoint.apiBase, endpoint.model, settings.style, context);
}

void EndpointTranscriptRefiner::cancel()
{
    m_chat->cancel();
    m_messages->cancel();
}

} // namespace speecher
