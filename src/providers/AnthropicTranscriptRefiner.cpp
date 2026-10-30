#include "providers/AnthropicTranscriptRefiner.h"

#include "core/EndpointUrl.h"
#include "providers/AnthropicApiRefiner.h"
#include "providers/AnthropicAuthProvider.h"
#include "providers/ClaudeCredentials.h"

#include <QDebug>

#include <memory>
#include <utility>

namespace speecher {

AnthropicTranscriptRefiner::AnthropicTranscriptRefiner(QObject *parent)
    : TranscriptRefiner(parent)
    , m_apiRefiner(new AnthropicApiRefiner(this))
{
    connect(m_apiRefiner, &AnthropicApiRefiner::delta, this, &AnthropicTranscriptRefiner::delta);
    connect(m_apiRefiner, &AnthropicApiRefiner::completed, this, &AnthropicTranscriptRefiner::completed);
    connect(m_apiRefiner, &AnthropicApiRefiner::failed, this, &AnthropicTranscriptRefiner::failed);
}

bool AnthropicTranscriptRefiner::requiresRefresh(const RefinementSettings &settings,
                                                 const AccountSettings &accounts) const
{
    return AnthropicAuthProvider::forRefinement(accounts, settings).requiresRefresh();
}

std::optional<RefinementRefreshJob> AnthropicTranscriptRefiner::createRefreshJob(const RefinementSettings &settings,
                                                                                 const AccountSettings &accounts)
{
    const AnthropicAuthProvider signIn = AnthropicAuthProvider::forRefinement(accounts, settings);
    auto accessToken = std::make_shared<QString>();
    RefinementRefreshJob job;
    job.showRefreshIndicator = signIn.requiresRefresh();
    job.run = [signIn, accessToken] {
        const AnthropicAuth auth = signIn.resolve(true);
        ClaudeCredentials::installedVersion();
        *accessToken = auth.accessToken;
        return RefinementRefreshResult{auth.ok, auth.error, auth.kind, auth.httpStatus};
    };
    job.apply = [this, accessToken](const RefinementRefreshResult &result) {
        if (result.ok) {
            m_accessToken = *accessToken;
            m_authResolvedByJob = true;
        } else {
            m_accessToken.clear();
        }
    };
    return job;
}

void AnthropicTranscriptRefiner::refresh(const RefinementSettings &settings, const AccountSettings &accounts)
{
    m_accessToken = AnthropicAuthProvider::forRefinement(accounts, settings).resolve(true).accessToken;
}

// Renewing an expired token is the refresh job's, on the worker: a renewal
// here would block this thread on the network and the keychain. Without a
// job's token, prepare() reads the sign-in as it stands.
RefinementPrepareResult AnthropicTranscriptRefiner::prepare(const RefinementSettings &settings,
                                                           const AccountSettings &accounts)
{
    if (std::exchange(m_authResolvedByJob, false)) {
        return {true, {}};
    }
    const AnthropicAuth auth = AnthropicAuthProvider::forRefinement(accounts, settings).resolve(false);
    m_accessToken = auth.accessToken;
    return {auth.ok, auth.error, auth.kind, auth.httpStatus};
}

void AnthropicTranscriptRefiner::refine(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const RefinementContext &context,
                                        const RefinementSettings &settings,
                                        const AccountSettings &accounts)
{
    qInfo().noquote() << "anthropic refinement mode=" + accounts.anthropicAuthMode + " model=" + settings.anthropicModel;
    const bool cliproxy = accounts.anthropicAuthMode == QStringLiteral("cliproxy");
    if (m_accessToken.isEmpty() || cliproxy) {
        const RefinementPrepareResult prepared = prepare(settings, accounts);
        if (!prepared.ok) {
            emit failed({prepared.kind, prepared.message, prepared.httpStatus});
            return;
        }
    }
    const bool remoteCliproxy = cliproxy && !settings.cliproxyBaseUrl.isEmpty();
    m_apiRefiner->refine(rawTranscript,
                         vocabulary,
                         settings.bindingVocabulary,
                         m_accessToken,
                         remoteCliproxy ? endpointApiBase(settings.cliproxyBaseUrl)
                                        : settings.anthropicEndpointBase,
                         settings.anthropicModel,
                         settings.anthropicEffort,
                         settings.anthropicFastMode,
                         settings.style,
                         context);
}

void AnthropicTranscriptRefiner::cancel()
{
    m_apiRefiner->cancel();
}

} // namespace speecher
