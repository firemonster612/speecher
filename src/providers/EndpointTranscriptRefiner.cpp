#include "providers/EndpointTranscriptRefiner.h"

#include "providers/AnthropicApiRefiner.h"
#include "providers/ChatCompletionsRefiner.h"
#include "providers/CustomEndpoints.h"

namespace speecher {

EndpointTranscriptRefiner::EndpointTranscriptRefiner(QObject *parent)
    : TranscriptRefiner(parent)
    , m_chat(new ChatCompletionsRefiner(label(), ChatCompletionsRefiner::Audience::Server, this))
    , m_messages(new AnthropicApiRefiner(this, 60000, 120000, label()))
{
    connect(m_chat, &ChatCompletionsRefiner::delta, this, &TranscriptRefiner::delta);
    connect(m_chat, &ChatCompletionsRefiner::completed, this, &TranscriptRefiner::completed);
    connect(m_chat, &ChatCompletionsRefiner::failed, this, &TranscriptRefiner::failed);
    connect(m_messages, &AnthropicApiRefiner::delta, this, &TranscriptRefiner::delta);
    connect(m_messages, &AnthropicApiRefiner::completed, this, &TranscriptRefiner::completed);
    connect(m_messages, &AnthropicApiRefiner::failed, this, &TranscriptRefiner::failed);
}

QString EndpointTranscriptRefiner::id() const
{
    return QStringLiteral("endpoint");
}

QString EndpointTranscriptRefiner::label() const
{
    return QStringLiteral("Custom Endpoint");
}

bool EndpointTranscriptRefiner::requiresRefresh(const RefinementSettings &) const
{
    return false;
}

void EndpointTranscriptRefiner::refresh(const RefinementSettings &)
{
}

RefinementPrepareResult EndpointTranscriptRefiner::prepare(const RefinementSettings &settings)
{
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings);
    if (endpoint.apiBase.isEmpty()) {
        return {false,
                settings.endpoint.preset == QStringLiteral("cliproxy")
                    ? QStringLiteral("Set the CLI Proxy API server URL for the custom endpoint.")
                    : QStringLiteral("Set the custom endpoint's server URL."),
                ProviderFailureKind::Unavailable};
    }
    if (endpoint.model.isEmpty()) {
        return {false, QStringLiteral("Choose a model for the custom endpoint."), ProviderFailureKind::Unavailable};
    }
    return {true, {}};
}

void EndpointTranscriptRefiner::refine(const QString &rawTranscript,
                                       const QStringList &vocabulary,
                                       const RefinementContext &context,
                                       const RefinementSettings &settings)
{
    const RefinementPrepareResult prepared = prepare(settings);
    if (!prepared.ok) {
        emit failed({prepared.kind, prepared.message});
        return;
    }
    const RefinementEndpoint endpoint = resolvedRefinementEndpoint(settings);
    if (endpoint.format == QStringLiteral("anthropic")) {
        m_messages->refineWithApiKey(rawTranscript, vocabulary, settings.bindingVocabulary,
                                     endpoint.apiKey, endpoint.apiBase, endpoint.model,
                                     settings.style, context);
        return;
    }
    m_chat->refine(rawTranscript, vocabulary, settings.bindingVocabulary, endpoint.apiKey,
                   endpoint.apiBase, endpoint.model, settings.style, context);
}

void EndpointTranscriptRefiner::cancel()
{
    m_chat->cancel();
    m_messages->cancel();
}

} // namespace speecher
