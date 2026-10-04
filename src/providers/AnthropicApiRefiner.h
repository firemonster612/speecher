#pragma once

#include "providers/StreamingRefinement.h"

#include <QStringList>

namespace speecher {

struct RefinementContext;

class AnthropicApiRefiner final : public QObject {
    Q_OBJECT

public:
    // label names the provider in failure messages.
    explicit AnthropicApiRefiner(QObject *parent = nullptr,
                                 int requestTimeoutMs = 20000,
                                 int absoluteDeadlineMs = 120000,
                                 const QString &label = QStringLiteral("Anthropic"));

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary,
                const QString &bearerToken,
                const QString &endpointBase,
                const QString &model,
                const QString &effort,
                bool fastMode,
                const QString &refinementStyle,
                const RefinementContext &context);
    // An Anthropic-compatible server reached with an API key: no Claude Code
    // identity, OAuth headers, thinking or fast mode.
    void refineWithApiKey(const QString &rawTranscript,
                          const QStringList &vocabulary,
                          const QStringList &bindingVocabulary,
                          const QString &apiKey,
                          const QString &endpointBase,
                          const QString &model,
                          const QString &refinementStyle,
                          const RefinementContext &context);
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const speecher::ProviderFailure &failure);

private:
    StreamingRefinement m_stream;
};

} // namespace speecher
