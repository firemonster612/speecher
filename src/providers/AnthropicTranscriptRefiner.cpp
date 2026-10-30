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

std::optional<PreparationJob> AnthropicTranscriptRefiner::createRefreshJob(const RefinementSettings &settings,
                                                                           const AccountSettings &accounts)
{
    // As in OpenAiTranscriptRefiner: a handed-over token is for one preparation.
    m_authResolvedByJob = false;
    const AnthropicAuthProvider signIn = AnthropicAuthProvider::forRefinement(accounts, settings);
    auto accessToken = std::make_shared<QString>();
    PreparationJob job;
    // Whether the token is due is read on the worker, which renews it; this
    // thread only guesses, for the indicator.
    job.showRefreshIndicator = signIn.expiredAsLastRead();
    job.run = [signIn, accessToken] {
        const AnthropicAuth auth = signIn.resolve(true);
        ClaudeCredentials::installedVersion();
        *accessToken = auth.accessToken;
        return auth.failure();
    };
    job.apply = [this, accessToken](const std::optional<ProviderFailure> &failure) {
        if (!failure) {
            m_accessToken = *accessToken;
            m_authResolvedByJob = true;
        } else {
            m_accessToken.clear();
        }
    };
    return job;
}

// Renewing an expired token is the refresh job's, on the worker: a renewal
// here would block this thread on the network and the keychain. Without a
// job's token, prepare() reads the sign-in as it stands.
std::optional<ProviderFailure> AnthropicTranscriptRefiner::prepare(const RefinementSettings &settings,
                                                                  const AccountSettings &accounts)
{
    if (std::exchange(m_authResolvedByJob, false)) {
        return std::nullopt;
    }
    const AnthropicAuth auth = AnthropicAuthProvider::forRefinement(accounts, settings).resolve(false);
    m_accessToken = auth.accessToken;
    return auth.failure();
}

void AnthropicTranscriptRefiner::refine(const QString &rawTranscript,
                                        const QStringList &vocabulary,
                                        const RefinementContext &context,
                                        const RefinementSettings &settings,
                                        const AccountSettings &accounts)
{
    qInfo().noquote() << "anthropic refinement mode=" + anthropicAuthModeName(accounts.anthropicAuthMode)
            + " model=" + settings.anthropicModel;
    const bool cliproxy = accounts.anthropicAuthMode == AnthropicAuthMode::Cliproxy;
    if (m_accessToken.isEmpty() || cliproxy) {
        if (const std::optional<ProviderFailure> failure = prepare(settings, accounts)) {
            emit failed(*failure);
            return;
        }
    }
    const bool remoteCliproxy = cliproxy && !settings.cliproxyBaseUrl.isEmpty();
    m_apiRefiner->refine(rawTranscript,
                         vocabulary,
                         context.bindingVocabulary,
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
