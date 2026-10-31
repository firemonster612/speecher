#include "providers/OpenAiTranscriptRefiner.h"

#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "providers/OpenAiRefiner.h"

#include <QDebug>

#include <memory>
#include <utility>

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

std::optional<PreparationJob> OpenAiTranscriptRefiner::createRefreshJob(const RefinementSettings &settings,
                                                                        const AccountSettings &accounts)
{
    // A sign-in an earlier job handed over belongs to the preparation it was
    // made for, which may never have come: settings can have changed since.
    m_authResolvedByJob = false;
    const OpenAiAuthProvider signIn = OpenAiAuthProvider::forRefinement(m_secretStore, accounts, settings);
    // This thread's store only starts a keyring read, so a settings API key
    // it hasn't read is read on the worker too; that read keeps the key in
    // memory for the dictations after.
    const bool unreadApiKey = accounts.openAiAuthMode == OpenAiAuthMode::Settings && m_secretStore
        && !m_secretStore->isSecretKnown(SecretStore::Secret::OpenAiApiKey);
    if (unreadApiKey) {
        m_secretStore->apiKey();
    }
    if (!signIn.mayUseCodexOauth() && !unreadApiKey) {
        return std::nullopt;
    }

    // Whether the sign-in is due is read on the worker, which renews it; this
    // thread only guesses, for the indicator. The worker resolves the whole
    // sign-in even when it isn't due, so prepare() reads no credentials here.
    auto resolved = std::make_shared<OpenAiAuth>();
    PreparationJob job;
    job.showRefreshIndicator = signIn.codexOauthExpiredAsLastRead();
    // Every source the mode allows, as prepare() resolves them, so a sign-in
    // that can't renew still leaves an API key to use. The worker reads the
    // keyring through a SecretStore of its own, never the GUI thread's.
    job.run = [settings, accounts, resolved]() -> std::optional<ProviderFailure> {
        SettingsStore source;
        *resolved = OpenAiAuthProvider::forRefinement(source.secrets(), accounts, settings).resolve();
        return resolved->failure();
    };
    job.apply = [this, resolved](const std::optional<ProviderFailure> &failure) {
        if (!failure && resolved->ok) {
            m_auth = *resolved;
            m_authResolvedByJob = true;
        }
    };
    return job;
}

std::optional<ProviderFailure> OpenAiTranscriptRefiner::prepare(const RefinementSettings &settings,
                                                               const AccountSettings &accounts)
{
    if (std::exchange(m_authResolvedByJob, false)) {
        return std::nullopt;
    }
    // Renewing an expired sign-in is the refresh job's, on the worker: a
    // renewal here would block this thread on the network and the credential
    // lock. Without a job's answer, prepare() reads the sign-in as it stands.
    m_auth = OpenAiAuthProvider::forRefinement(m_secretStore, accounts, settings).resolve(false);
    return m_auth.failure();
}

void OpenAiTranscriptRefiner::refine(const QString &rawTranscript,
                                     const QStringList &vocabulary,
                                     const RefinementContext &context,
                                     const RefinementSettings &settings,
                                     const AccountSettings &accounts)
{
    // CLI Proxy API rewrites its token files as it refreshes them, so a cached
    // token can go stale while the file stays valid; reload on every request.
    if (!m_auth.ok || accounts.openAiAuthMode == OpenAiAuthMode::Cliproxy) {
        if (const std::optional<ProviderFailure> failure = prepare(settings, accounts)) {
            emit failed(*failure);
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
                      settings.openAiModel,
                      settings.openAiEffort,
                      settings.openAiSpeed,
                      settings.style,
                      context);
}

void OpenAiTranscriptRefiner::cancel()
{
    m_refiner->cancel();
}

} // namespace speecher
