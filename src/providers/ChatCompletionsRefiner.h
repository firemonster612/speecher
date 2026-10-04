#pragma once

#include "providers/StreamingRefinement.h"

#include <QSet>
#include <QStringList>

namespace speecher {

struct RefinementContext;

// Refinement through an OpenAI-compatible POST {base}/chat/completions, the
// API every self-hosted runner shares. Asks the server to skip reasoning, and
// repeats a request once without those fields when the server rejects them.
class ChatCompletionsRefiner final : public QObject {
    Q_OBJECT

public:
    // Who the requests are shaped for.
    enum class Audience {
        // A server whose model can follow the full prompt.
        Server,
        // The small models a Local Runner serves: the compact prompt, sampled
        // at temperature 0 so a 1B model does not wander off the transcript.
        SmallLocalModel,
    };

    // label names the provider in failure messages.
    ChatCompletionsRefiner(const QString &label,
                           Audience audience,
                           QObject *parent = nullptr,
                           int requestTimeoutMs = 60000,
                           int absoluteDeadlineMs = 120000);

    void refine(const QString &rawTranscript,
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
    void post(bool withReasoningFields);

    Audience m_audience;
    StreamingRefinement m_stream;
    std::function<StreamingRefinement::Request(bool withReasoningFields)> m_buildRequest;
    QString m_serverModel;
    bool m_sentReasoningFields = false;
    bool m_streamedOutput = false;
    // Server and model pairs that refused the reasoning fields, so later
    // requests leave them out instead of failing first every time.
    QSet<QString> m_rejectsReasoningFields;
};

} // namespace speecher
