#include "frontend/mac/MacCustomRows.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "providers/CliProxyCredentials.h"

namespace speecher::mac {

namespace {

const QString kCliProxyAuthMode = QStringLiteral("cliproxy");
const QString kProfileColumn = QStringLiteral("profile");
const QString kProfileIdKey = QStringLiteral("profileId");
const QString kCleanupColumn = QStringLiteral("cleanup");
const QString kToneColumn = QStringLiteral("tone");

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

QList<RowOption> cliproxyAccounts(const QString &type,
                                  const QString &selected,
                                  const SettingsStore &store)
{
    const QString directory = store.cliproxyOauthDir();
    const QList<CliProxyAccount> accounts = CliProxyCredentials::listAccounts(directory, type);
    QList<RowOption> options;
    // With several accounts and none chosen yet, force an explicit choice
    // instead of silently pinning whichever file sorts first.
    if (selected.isEmpty() && accounts.size() > 1) {
        options.append({QString(), QStringLiteral("Choose an account…")});
    }
    bool selectedFound = selected.isEmpty();
    for (const CliProxyAccount &account : accounts) {
        options.append({account.fileName,
                        account.expired ? account.label + QStringLiteral(" (expired)") : account.label,
                        account.disabled ? QStringLiteral("Disabled in CLI Proxy API") : QString(),
                        !account.disabled});
        selectedFound = selectedFound || account.fileName == selected;
    }
    // Keep a stored selection visible even if its file is currently missing.
    if (!selectedFound) {
        options.append({selected, selected + QStringLiteral(" (missing)")});
    }
    if (options.isEmpty()) {
        options.append({QString(), QStringLiteral("No accounts found"), directory, false});
    }
    return options;
}

QList<RowOption> cleanupStrengths()
{
    return {
        {QStringLiteral("none"), QStringLiteral("None")},
        {QStringLiteral("light_cleanup"), QStringLiteral("Light")},
        {QStringLiteral("balanced"), QStringLiteral("Medium")},
        {QStringLiteral("strong_polish"), QStringLiteral("High")},
    };
}

QList<RowOption> writingTones()
{
    return {
        {QStringLiteral("none"), QStringLiteral("No tone override")},
        {QStringLiteral("formal"), QStringLiteral("Formal")},
        {QStringLiteral("casual"), QStringLiteral("Casual")},
        {QStringLiteral("very_casual"), QStringLiteral("Very casual")},
        {QStringLiteral("excited"), QStringLiteral("Excited")},
        {QStringLiteral("gen_z"), QStringLiteral("Gen Z")},
    };
}

} // namespace

QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store)
{
    if (rowId == QStringLiteral("outputMethod")) {
        return outputMethods();
    }
    if (rowId == QStringLiteral("openAiAuthMode")) {
        return {
            {QStringLiteral("auto"), QStringLiteral("Automatic")},
            {QStringLiteral("codex_api_key"), QStringLiteral("Codex API key")},
            {QStringLiteral("codex_oauth"), QStringLiteral("Codex OAuth")},
            {QStringLiteral("env"), QStringLiteral("OPENAI_API_KEY")},
            {QStringLiteral("settings"), QStringLiteral("App settings key")},
            {kCliProxyAuthMode, QStringLiteral("CLI Proxy API")},
        };
    }
    if (rowId == QStringLiteral("anthropicAuthMode")) {
        return {
            {QStringLiteral("oauth"), QStringLiteral("Claude OAuth")},
            {kCliProxyAuthMode, QStringLiteral("CLI Proxy API")},
        };
    }
    if (rowId == QStringLiteral("openAiCliproxyAccount")) {
        return cliproxyAccounts(QStringLiteral("codex"), draft.refinement.openAiCliproxyAccount, store);
    }
    if (rowId == QStringLiteral("anthropicCliproxyAccount")) {
        return cliproxyAccounts(QStringLiteral("claude"), draft.refinement.anthropicCliproxyAccount, store);
    }
    return {};
}

CollectionDescriptor writingProfileGrid()
{
    CollectionDescriptor grid;
    grid.columns = {
        {kProfileColumn, QStringLiteral("Profile"), ColumnKind::ReadOnly},
        {kCleanupColumn, QStringLiteral("Cleanup"), ColumnKind::Choice, cleanupStrengths},
        {kToneColumn, QStringLiteral("Tone"), ColumnKind::Choice, writingTones, true},
    };
    // The profiles are the ones that exist, so the stored list only says what
    // each of them was set to.
    grid.records = [](const AppSettings &settings) {
        QList<QVariantMap> records;
        for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
            const WritingProfileSettings chosen =
                writingProfileSettingsFor(settings.refinement.writingProfiles, fallback.profile);
            records.append({{kProfileColumn, writingProfileLabel(fallback.profile)},
                            {kProfileIdKey, writingProfileName(fallback.profile)},
                            {kCleanupColumn, chosen.cleanupStrength},
                            {kToneColumn, chosen.tone}});
        }
        return records;
    };
    grid.apply = [](AppSettings &settings, const QList<QVariantMap> &records) {
        QList<WritingProfileSettings> profiles;
        for (const QVariantMap &record : records) {
            profiles.append({writingProfileFromName(record.value(kProfileIdKey).toString()),
                             record.value(kCleanupColumn).toString(),
                             record.value(kToneColumn).toString()});
        }
        settings.refinement.writingProfiles = profiles;
    };
    return grid;
}

} // namespace speecher::mac
