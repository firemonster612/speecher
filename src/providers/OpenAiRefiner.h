#pragma once

#include "providers/StreamingRefinement.h"

#include <QStringList>

namespace speecher {

struct RefinementContext;

class OpenAiRefiner : public QObject {
    Q_OBJECT

public:
    explicit OpenAiRefiner(QObject *parent = nullptr,
                           int requestTimeoutMs = 20000,
                           int absoluteDeadlineMs = 120000);

    void refine(const QString &rawTranscript,
                const QStringList &vocabulary,
                const QStringList &bindingVocabulary,
                const QString &bearerToken,
                const QString &organization,
                const QString &project,
                const QString &endpointBase,
                const QString &accountId,
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
