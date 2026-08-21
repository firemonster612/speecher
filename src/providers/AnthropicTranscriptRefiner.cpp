#include "providers/AnthropicTranscriptRefiner.h"

#include "providers/AnthropicApiRefiner.h"
#include "providers/ClaudeCredentials.h"
#include "providers/CliProxyCredentials.h"

#include <QDebug>

#include <memory>

namespace speecher {
namespace {

RefinementPrepareResult loadClaudeOauthToken(const RefinementSettings &settings,
                                             QString *accessToken,
                                             bool refreshExpired)
{
    if (settings.anthropicAuthMode == QStringLiteral("cliproxy")) {
        // Remote proxy: authenticate with the CLI Proxy API server key; the
        // server picks the account and refreshes its own oauth tokens.
        if (!settings.cliproxyBaseUrl.isEmpty()) {
            if (settings.cliproxyApiKey.isEmpty()) {
                if (accessToken) {
                    accessToken->clear();
                }
                return {false, QStringLiteral("CLI Proxy API key is not set (cliproxy/apiKey)")};
            }
            if (accessToken) {
                *accessToken = settings.cliproxyApiKey;
            }
            return {true, QString()};
        }
        const CliProxyCredentialResult credentials =
            CliProxyCredentials::load(settings.cliproxyOauthDir, QStringLiteral("claude"), settings.anthropicCliproxyAccount);
        if (accessToken) {
            *accessToken = credentials.ok ? credentials.accessToken : QString();
        }
        return {credentials.ok, credentials.error};
    }
    const ClaudeCredentialResult credentials = ClaudeCredentials::load(
        settings.claudeCredentialsPath,
        refreshExpired);
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
{
    connect(m_apiRefiner, &AnthropicApiRefiner::delta, this, &AnthropicTranscriptRefiner::delta);
    connect(m_apiRefiner, &AnthropicApiRefiner::completed, this, &AnthropicTranscriptRefiner::completed);
    connect(m_apiRefiner, &AnthropicApiRefiner::failed, this, &AnthropicTranscriptRefiner::failed);
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
    if (settings.anthropicAuthMode == QStringLiteral("cliproxy")) {
        // CLI Proxy API refreshes its own tokens; refine() reloads them per request.
        return false;
    }
    return ClaudeCredentials::requiresRefresh(settings.claudeCredentialsPath);
}

bool AnthropicTranscriptRefiner::supportsScreenshotContext(const RefinementSettings &settings) const
{
    return !settings.anthropicModel.trimmed().isEmpty();
}

std::optional<RefinementRefreshJob> AnthropicTranscriptRefiner::createRefreshJob(const RefinementSettings &settings)
{
    auto accessToken = std::make_shared<QString>();
    RefinementRefreshJob job;
    job.showRefreshIndicator = requiresRefresh(settings);
    job.run = [settings, accessToken] {
        const RefinementPrepareResult result = loadClaudeOauthToken(settings, accessToken.get(), true);
        ClaudeCredentials::installedVersion();
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
    loadClaudeOauthToken(settings, &m_accessToken, true);
}

RefinementPrepareResult AnthropicTranscriptRefiner::prepare(const RefinementSettings &settings)
{
    return loadClaudeOauthToken(settings, &m_accessToken, false);
}

void AnthropicTranscriptRefiner::refine(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const RefinementContext &context,
                                        const RefinementSettings &settings)
{
    qInfo().noquote() << "anthropic refinement mode=" + settings.anthropicAuthMode + " model=" + settings.anthropicModel;
    if (m_accessToken.isEmpty() || settings.anthropicAuthMode == QStringLiteral("cliproxy")) {
        const RefinementPrepareResult prepared = prepare(settings);
        if (!prepared.ok) {
            emit failed(prepared.message);
            return;
        }
    }
    const bool remoteCliproxy = settings.anthropicAuthMode == QStringLiteral("cliproxy")
        && !settings.cliproxyBaseUrl.isEmpty();
    m_apiRefiner->refine(rawTranscript,
                         vocabulary,
                         settings.bindingVocabulary,
                         m_accessToken,
                         remoteCliproxy ? settings.cliproxyBaseUrl + QStringLiteral("/v1")
                                        : settings.anthropicEndpointBase,
                         settings.anthropicModel,
                         settings.anthropicEffort,
                         settings.style,
                         context);
}

void AnthropicTranscriptRefiner::cancel()
{
    m_apiRefiner->cancel();
}

} // namespace speecher
