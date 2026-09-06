#include "frontend/mac/MacCustomRows.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "providers/ClaudeCredentials.h"
#include "frontend/ProviderOptions.h"

namespace speecher::mac {

namespace {

const QString kCliProxyAuthMode = QStringLiteral("cliproxy");

QList<RowOption> outputMethods()
{
    // No ydotool entry: the virtual keyboard is Linux's, and a method macOS
    // cannot offer has no business being offered here.
    QList<RowOption> methods;
    for (const char *method : {OutputMethod::Automatic,
                               OutputMethod::DirectInsert,
                               OutputMethod::MacPaste,
                               OutputMethod::QtClipboard}) {
        const QString id = QString::fromLatin1(method);
        methods.append({id, OutputMethod::label(id)});
    }
    return methods;
}

} // namespace

QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store)
{
    if (rowId == QStringLiteral("outputMethod")) {
        return outputMethods();
    }
    if (rowId == QStringLiteral("openAiCliproxyAccount")) {
        return cliproxyAccountOptions(QStringLiteral("codex"), draft.refinement.openAiCliproxyAccount, store.cliproxyOauthDir());
    }
    if (rowId == QStringLiteral("anthropicCliproxyAccount")) {
        return cliproxyAccountOptions(QStringLiteral("claude"), draft.refinement.anthropicCliproxyAccount, store.cliproxyOauthDir());
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
