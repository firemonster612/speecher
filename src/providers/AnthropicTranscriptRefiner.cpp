#include "providers/AnthropicTranscriptRefiner.h"

#include "providers/AnthropicApiRefiner.h"
#include "providers/ClaudeCodeSessionRefiner.h"
#include "providers/ClaudeCredentials.h"

#include <QDebug>

#include <memory>

namespace speecher {
namespace {

RefinementPrepareResult loadClaudeOauthToken(const RefinementSettings &settings, QString *accessToken)
{
    const ClaudeCredentialResult credentials = ClaudeCredentials::load(settings.claudeCredentialsPath, true);
    if (!credentials.ok) {
        if (accessToken) {
            accessToken->clear();
        }
        return {false, credentials.error};
    }
    if (accessToken) {
        *accessToken = credentials.accessToken;
    }
    return {true, QString()};
}

} // namespace

AnthropicTranscriptRefiner::AnthropicTranscriptRefiner(QObject *parent)
    : TranscriptRefiner(parent)
    , m_apiRefiner(new AnthropicApiRefiner(this))
    , m_codeSession(new ClaudeCodeSessionRefiner(this))
{
    connect(m_apiRefiner, &AnthropicApiRefiner::delta, this, &AnthropicTranscriptRefiner::delta);
    connect(m_apiRefiner, &AnthropicApiRefiner::completed, this, &AnthropicTranscriptRefiner::completed);
    connect(m_apiRefiner, &AnthropicApiRefiner::failed, this, &AnthropicTranscriptRefiner::failed);
    connect(m_codeSession, &ClaudeCodeSessionRefiner::delta, this, &AnthropicTranscriptRefiner::delta);
    connect(m_codeSession, &ClaudeCodeSessionRefiner::completed, this, &AnthropicTranscriptRefiner::completed);
    connect(m_codeSession, &ClaudeCodeSessionRefiner::failed, this, &AnthropicTranscriptRefiner::failed);
}

QString AnthropicTranscriptRefiner::id() const
{
    return QStringLiteral("anthropic");
}

QString AnthropicTranscriptRefiner::label() const
{
    return QStringLiteral("Anthropic");
}

bool AnthropicTranscriptRefiner::requiresRefresh(const RefinementSettings &settings) const
{
    return usesOauth(settings) && ClaudeCredentials::requiresRefresh(settings.claudeCredentialsPath);
}

std::optional<RefinementRefreshJob> AnthropicTranscriptRefiner::createRefreshJob(const RefinementSettings &settings)
{
    if (!requiresRefresh(settings)) {
        return std::nullopt;
    }

    auto accessToken = std::make_shared<QString>();
    RefinementRefreshJob job;
    job.showRefreshIndicator = true;
    job.run = [settings, accessToken] {
        const RefinementPrepareResult result = loadClaudeOauthToken(settings, accessToken.get());
        return RefinementRefreshResult{result.ok, result.message};
    };
    job.apply = [this, accessToken](const RefinementRefreshResult &result) {
        if (result.ok) {
            m_accessToken = *accessToken;
        } else {
            m_accessToken.clear();
        }
    };
    return job;
}

void AnthropicTranscriptRefiner::refresh(const RefinementSettings &settings)
{
    if (!usesOauth(settings)) {
        return;
    }
    loadClaudeOauthToken(settings, &m_accessToken);
}

RefinementPrepareResult AnthropicTranscriptRefiner::prepare(const RefinementSettings &settings)
{
    if (usesOauth(settings)) {
        return loadClaudeOauthToken(settings, &m_accessToken);
    }

    QString error;
    if (!m_codeSession->prepare(settings.anthropicModel, settings.anthropicEffort, settings.style, &error)) {
        return {false, error};
    }
    return {true, QString()};
}

void AnthropicTranscriptRefiner::refine(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const RefinementSettings &settings)
{
    if (usesOauth(settings)) {
        qInfo().noquote() << "anthropic refinement mode=oauth model=" + settings.anthropicModel;
        if (m_accessToken.isEmpty()) {
            const RefinementPrepareResult prepared = prepare(settings);
            if (!prepared.ok) {
                emit failed(prepared.message);
                return;
            }
        }
        m_apiRefiner->refine(rawTranscript,
                             vocabulary,
                             settings.bindingVocabulary,
                             m_accessToken,
                             settings.anthropicEndpointBase,
                             settings.anthropicModel,
                             settings.anthropicEffort,
                             settings.style);
        return;
    }

    qInfo().noquote() << "anthropic refinement mode=claude_code model=" + settings.anthropicModel
                      << "effort=" + settings.anthropicEffort;
    m_codeSession->refine(rawTranscript,
                          vocabulary,
                          settings.bindingVocabulary);
}

void AnthropicTranscriptRefiner::cancel()
{
    m_apiRefiner->cancel();
    m_codeSession->cancel();
}

bool AnthropicTranscriptRefiner::usesOauth(const RefinementSettings &settings) const
{
    return settings.anthropicAuthMode == QStringLiteral("oauth");
}

} // namespace speecher
