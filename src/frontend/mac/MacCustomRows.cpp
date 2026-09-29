#include "frontend/mac/MacCustomRows.h"

#include "core/SettingsStore.h"
#include "core/Target.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ProviderSignIn.h"

namespace speecher::mac {

namespace {

const QString kCliProxyAuthMode = QStringLiteral("cliproxy");

} // namespace

QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store)
{
    if (rowId == QStringLiteral("openAiCliproxyAccount")) {
        return cliproxyAccountOptions(ProviderSignIn::cliproxyAccountType(QStringLiteral("openai")),
                                      draft.refinement.openAiCliproxyAccount,
                                      store.cliproxyOauthDir());
    }
    if (rowId == QStringLiteral("anthropicCliproxyAccount")) {
        return cliproxyAccountOptions(ProviderSignIn::cliproxyAccountType(QStringLiteral("anthropic")),
                                      draft.refinement.anthropicCliproxyAccount,
                                      store.cliproxyOauthDir());
    }
    return authModeOptions(rowId);
}

QString anthropicCredentialStatus(const AppSettings &draft,
                                  const SettingsStore &store)
{
    if (draft.refinement.anthropicAuthMode == kCliProxyAuthMode) {
        return {};
    }
    const ClaudeCredentialResult credentials = ClaudeCredentials::load(
        store.claudeCredentialsPath(), false);
    return credentials.ok ? QStringLiteral("Signed in with Claude Code")
                          : credentials.error;
}

} // namespace speecher::mac
