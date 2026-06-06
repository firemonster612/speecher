#include "providers/OpenAiTranscriptRefiner.h"

#include "providers/OpenAiRefiner.h"

#include <QDebug>

namespace speecher {

OpenAiTranscriptRefiner::OpenAiTranscriptRefiner(SecretStore *secretStore, QObject *parent)
    : TranscriptRefiner(parent)
    , m_secretStore(secretStore)
    , m_refiner(new OpenAiRefiner(this))
{
    connect(m_refiner, &OpenAiRefiner::delta, this, &OpenAiTranscriptRefiner::delta);
    connect(m_refiner, &OpenAiRefiner::completed, this, &OpenAiTranscriptRefiner::completed);
    connect(m_refiner, &OpenAiRefiner::failed, this, &OpenAiTranscriptRefiner::failed);
}

QString OpenAiTranscriptRefiner::id() const
{
    return QStringLiteral("openai");
}

QString OpenAiTranscriptRefiner::label() const
{
    return QStringLiteral("OpenAI");
}

bool OpenAiTranscriptRefiner::requiresRefresh(const RefinementSettings &settings) const
{
    return OpenAiAuthProvider(m_secretStore, settings.openAiAuthMode).requiresCodexOauthRefresh();
}

void OpenAiTranscriptRefiner::refresh(const RefinementSettings &settings)
{
    const OpenAiAuth refreshed = OpenAiAuthProvider(m_secretStore, settings.openAiAuthMode).refreshCodexOauth();
    if (!refreshed.ok) {
        qWarning().noquote() << "codex oauth refresh unavailable status=" + refreshed.status;
    }
}

RefinementPrepareResult OpenAiTranscriptRefiner::prepare(const RefinementSettings &settings)
{
    m_auth = OpenAiAuthProvider(m_secretStore, settings.openAiAuthMode).resolve();
    return {m_auth.ok, m_auth.status};
}

void OpenAiTranscriptRefiner::refine(const QString &rawTranscript,
                                     const QStringList &vocabulary,
                                     const RefinementSettings &settings)
{
    if (!m_auth.ok) {
        const RefinementPrepareResult prepared = prepare(settings);
        if (!prepared.ok) {
            emit failed(prepared.message);
            return;
        }
    }

    m_refiner->refine(rawTranscript,
                      vocabulary,
                      m_auth.bearerToken,
                      m_auth.organization,
                      m_auth.project,
                      m_auth.endpointBase,
                      m_auth.accountId,
                      m_auth.chatgptBackend,
                      settings.openAiModel,
                      settings.style,
                      settings.outputFormat);
}

void OpenAiTranscriptRefiner::cancel()
{
    m_refiner->cancel();
}

} // namespace speecher
