#pragma once

#include "providers/StreamingRefinement.h"

#include <QStringList>

namespace speecher {

struct RefinementContext;

class AnthropicApiRefiner final : public QObject {
    Q_OBJECT

public:
    explicit AnthropicApiRefiner(QObject *parent = nullptr,
                                 int requestTimeoutMs = 20000,
                                 int absoluteDeadlineMs = 120000);

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
    void cancel();

signals:
    void delta(const QString &text);
    void completed(const QString &text);
    void failed(const QString &message);

private:
    StreamingRefinement m_stream;
};

} // namespace speecher
