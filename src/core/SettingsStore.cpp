#include "core/SettingsStore.h"
#include "core/EndpointSettings.h"
#include "core/OutputMethod.h"
#include "core/ProviderTable.h"
#include "core/SecretStore.h"
#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/SettingsKeys.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <memory>
#include <utility>

namespace speecher {

namespace {

constexpr int identityMigrationVersion = 1;

// The settings Speecher kept before its app id changed (ADR 0002), which the
// identity migration imports. Opened in the format SettingsFile opens, which
// tests point at a folder of their own; QSettings(organization, application)
// would always open the person's file.
std::unique_ptr<QSettings> olderSettings()
{
    return std::make_unique<QSettings>(QSettings::defaultFormat(), QSettings::UserScope,
                                       QStringLiteral("local.speecher"), SettingsKeys::application());
}

// Deletes those settings, which a start that finds no settings of its own
// imports again, keys and all, and their emptied folder. Returns false, with
// their file in path, when they won't go.
bool deleteOlderSettings(QString *path)
{
    QString file;
    {
        const std::unique_ptr<QSettings> older = olderSettings();
        older->clear();
        older->sync();
        file = older->fileName();
    }
    // Judged by the file alone: QSettings keeps the error a read-only or
    // malformed file gave it, though the folder lets the file go. A folder
    // that can't be searched hides whether the file is still there.
#ifdef Q_OS_UNIX
    const QFileInfo folder(QFileInfo(file).absolutePath());
    const bool hidden = folder.exists() && !folder.isExecutable();
#else
    const bool hidden = false;
#endif
    if (hidden || (QFile::exists(file) && !QFile::remove(file))) {
        if (path) {
            *path = file;
        }
        return false;
    }
    QDir().rmdir(QFileInfo(file).absolutePath());
    return true;
}

// The files that wouldn't go or save, for a removal's error: the older
// settings' file, then the current one's.
QString failedFiles(const QString &olderFile, const QString &file)
{
    return olderFile.isEmpty() || file.isEmpty() ? olderFile + file
                                                 : olderFile + QStringLiteral(" or the one at ") + file;
}

} // namespace

bool migrateSettingsIdentity(QSettings &newSettings, QSettings &oldSettings, QString *error)
{
    newSettings.setFallbacksEnabled(false);
    oldSettings.setFallbacksEnabled(false);
    if (newSettings.value(SettingsKeys::IdentityMigrationVersion).toInt() >= identityMigrationVersion) {
        return true;
    }

    const QStringList oldKeys = oldSettings.allKeys();
    const bool existingConfig = !newSettings.allKeys().isEmpty() || !oldKeys.isEmpty();
    for (const QString &key : oldKeys) {
        if (!newSettings.contains(key)) {
            newSettings.setValue(key, oldSettings.value(key));
        }
    }
    if (existingConfig && !newSettings.contains(SettingsKeys::UpdatesLastRunVersion)) {
        newSettings.setValue(SettingsKeys::UpdatesLastRunVersion, QStringLiteral("0.1.0"));
    }
    newSettings.sync();
    if (newSettings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Could not persist migrated settings");
        }
        return false;
    }

    newSettings.setValue(SettingsKeys::IdentityMigrationVersion, identityMigrationVersion);
    newSettings.sync();
    if (newSettings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("Could not record settings migration");
        }
        return false;
    }
    return true;
}

void migrateRefinementModels(QSettings &settings)
{
    constexpr int currentMigrationVersion = 2;
    const int version = settings.value(SettingsKeys::RefinementModelMigrationVersion).toInt();
    if (version >= currentMigrationVersion) {
        return;
    }
    const auto replace = [&settings](const QString &key, const QString &from, const QString &to) {
        if (settings.value(key).toString().trimmed() == from) {
            settings.setValue(key, to);
        }
    };
    if (version < 1) {
        replace(SettingsKeys::OpenAiModel, QStringLiteral("gpt-5.6-luna"), QStringLiteral("gpt-6-luna"));
        replace(SettingsKeys::AnthropicModel, QStringLiteral("claude-sonnet-5"),
                QStringLiteral("claude-opus-5-5"));
    }
    // Someone who picked Sonnet 5 after the first migration keeps Sonnet, on
    // the model that replaced it in the picker.
    replace(SettingsKeys::AnthropicModel, QStringLiteral("claude-sonnet-5"),
            QStringLiteral("claude-sonnet-5-5"));
    settings.setValue(SettingsKeys::RefinementModelMigrationVersion, currentMigrationVersion);
}

void migrateOutputMethod(QSettings &settings)
{
    const QString method = OutputMethod::normalized(settings.value(SettingsKeys::OutputMethod).toString());
    PasteMethod paste;
    if (method == QString::fromLatin1(OutputMethod::DirectInsert)) {
        paste = PasteMethod::DirectInsert;
    } else if (method == QString::fromLatin1(OutputMethod::WlCopy)
               || method == QString::fromLatin1(OutputMethod::QtClipboard)) {
        paste = PasteMethod::ClipboardOnly;
    } else {
        return;
    }
    // Under these methods TextDelivery never pasted: a rule that said to paste
    // meant inserting only, or copying only. A Direct insertion or Clipboard
    // only rule already behaved as it does under Automatic. Only methods
    // change, so the rule that wins for a target is the same one as before.
    QList<PasteRule> rules = pasteRulesFromJson(settings.value(SettingsKeys::PasteRules).toByteArray());
    for (PasteRule &rule : rules) {
        if (rule.method == PasteMethod::StandardPaste || rule.method == PasteMethod::TerminalPaste) {
            rule.method = paste;
        }
    }
    settings.setValue(SettingsKeys::PasteRules, pasteRulesToJson(rules));
    settings.setValue(SettingsKeys::OutputMethod, QString::fromLatin1(OutputMethod::Automatic));
}

void migrateSavedSettings()
{
    // The format SettingsFile opens, as olderSettings does.
    QSettings newSettings(QSettings::defaultFormat(), QSettings::UserScope,
                          QString::fromLatin1(SettingsKeys::Organization), SettingsKeys::application());
    QString error;
    // Before anything is written: the identity migration copies in the keys
    // the older settings kept.
    if (!makeSettingsPrivate(newSettings, &error)) {
        qWarning().noquote() << "Settings: not migrated, since" << error;
        return;
    }
    const std::unique_ptr<QSettings> older = olderSettings();
    // An older Speecher left its file, keys and all, open to others.
    if (QFile::exists(older->fileName()) && !makeSettingsPrivate(*older, &error)) {
        qWarning().noquote() << "Settings: the older settings stay readable, since" << error;
    }
    if (!migrateSettingsIdentity(newSettings, *older, &error)) {
        qWarning().noquote() << error;
    }
    migrateRefinementModels(newSettings);
    migrateOutputMethod(newSettings);
}

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , SettingsCodecs()
{
    m_secrets = new SecretStore(this, this);
}

bool SettingsStore::deleteAll(QString *path)
{
    // Before close, which points every QSettings made afterwards at a
    // throwaway folder.
    QString olderFile;
    if (!deleteOlderSettings(&olderFile)) {
        QString file;
        keepOnlyDeletionMarkers(&file);
        if (path) {
            *path = failedFiles(olderFile, file);
        }
        return false;
    }
    if (m_settings.close({})) {
        return true;
    }
    if (path) {
        *path = m_settings.fileName();
    }
    return false;
}

bool SettingsStore::deleteAllButDeletionMarkers(QString *path)
{
    QString olderFile;
    const bool olderDeleted = deleteOlderSettings(&olderFile);
    QString file;
    const bool kept = keepOnlyDeletionMarkers(&file);
    if (path) {
        *path = failedFiles(olderFile, file);
    }
    if (!kept || !olderDeleted) {
        return false;
    }
    // As after deleteAll, nothing set from now on brings a setting or a key
    // back. Only now: a removal tried again must find the files that stayed.
    QVariantMap markers;
    for (const QString &key : m_settings.allKeys()) {
        markers.insert(key, m_settings.value(key));
    }
    m_settings.close(markers);
    return true;
}

bool SettingsStore::keepOnlyDeletionMarkers(QString *path)
{
    const QStringList markers = m_secrets->deletionMarkers();
    for (const QString &key : m_settings.allKeys()) {
        if (!markers.contains(key)) {
            m_settings.remove(key);
        }
    }
    // So the next start takes neither the markers nor older settings that
    // wouldn't go for settings to import.
    m_settings.setValue(SettingsKeys::IdentityMigrationVersion, identityMigrationVersion);
    if (m_settings.save()) {
        return true;
    }
    if (path) {
        *path = m_settings.fileName();
    }
    return false;
}

SecretStore *SettingsStore::secrets() const
{
    return m_secrets;
}

AppSettings SettingsStore::snapshot() const
{
    return snapshotReading(true);
}

AppSettings SettingsStore::dictationSnapshot() const
{
    return snapshotReading(false);
}

namespace {

// A keyring key and the field of the settings it fills.
struct ProviderSecret {
    SecretStore::Secret secret;
    QString &(*field)(AppSettings &);
};

// The keyring keys a provider reads in a role that the snapshot has not read.
QList<ProviderSecret> unreadProviderSecrets(const AppSettings &settings, ProviderRole role,
                                            const QString &providerId)
{
    using Secret = SecretStore::Secret;
    const std::optional<ProviderId> provider = parseProviderId(providerId);
    const bool endpoint = provider == ProviderId::Endpoint;
    QList<ProviderSecret> secrets;
    // Speech uses local OAuth account files, never the remote proxy API key.
    if (role == ProviderRole::Speech && endpoint) {
        secrets.append({Secret::SpeechEndpointKey, [](AppSettings &s) -> QString & { return s.speech.endpoint.apiKey; }});
    }
    const RefinementSettings &refinement = settings.refinement;
    const bool refines = role == ProviderRole::Refinement;
    const bool custom = endpoint && refinement.endpoint.preset.isEmpty();
    const auto endpointKey = SecretStore::settingsKey(Secret::RefinementEndpointKey);
    if (refines && custom) {
        secrets.append(
            {Secret::RefinementEndpointKey, [](AppSettings &s) -> QString & { return s.refinement.endpoint.apiKey; }});
    }
    // A Custom endpoint needs the proxy's key only once its own is known to
    // be empty, as refinementEndpointKey picks.
    const bool endpointProxyKey = endpoint
        && (!custom || (!settings.unreadSecretKeys.contains(endpointKey) && usesProxyKey(refinement)));
    const bool remoteProxy = !refinement.cliproxyBaseUrl.isEmpty()
        && ((provider == ProviderId::OpenAi && settings.accounts.openAiAuthMode == OpenAiAuthMode::Cliproxy)
            || (provider == ProviderId::Anthropic && settings.accounts.anthropicAuthMode == AnthropicAuthMode::Cliproxy));
    if (refines && (endpointProxyKey || remoteProxy)) {
        secrets.append({Secret::CliproxyApiKey, [](AppSettings &s) -> QString & { return s.refinement.cliproxyApiKey; }});
    }
    secrets.removeIf([&settings](const ProviderSecret &secret) {
        return !settings.unreadSecretKeys.contains(SecretStore::settingsKey(secret.secret));
    });
    return secrets;
}

} // namespace

bool SettingsStore::hasUnreadProviderSecrets(const AppSettings &settings, ProviderRole role, const QString &providerId)
{
    return !unreadProviderSecrets(settings, role, providerId).isEmpty();
}

QString SettingsStore::resolveProviderSecrets(AppSettings &settings, ProviderRole role, const QString &providerId)
{
    if (!hasUnreadProviderSecrets(settings, role, providerId)) return {};

    // Own QSettings and SecretStore in this worker; never access the GUI cache
    // or its in-flight prefetch jobs from another thread.
    SettingsStore source;
    // One at a time: what one read finds decides the next, as the Custom
    // endpoint's own key decides whether the proxy's is needed.
    for (QList<ProviderSecret> unread = unreadProviderSecrets(settings, role, providerId); !unread.isEmpty();
         unread = unreadProviderSecrets(settings, role, providerId)) {
        const auto &[secret, field] = unread.first();
        QString &value = field(settings);
        value = source.secrets()->secret(secret);
        if (const QString failure = source.secrets()->readFailure(secret); value.isEmpty() && !failure.isEmpty()) {
            return failure;
        }
        settings.unreadSecretKeys.removeAll(SecretStore::settingsKey(secret));
    }
    return {};
}

// Preserve unread state in the draft even if prefetch completes before save.
AppSettings SettingsStore::snapshotReading(bool waitForKeyring) const
{
    AppSettings settings = SettingsCodecs::snapshot();
    const auto read = [this, &settings, waitForKeyring](SecretStore::Secret secret) {
        const QString value = waitForKeyring ? m_secrets->secret(secret) : m_secrets->cachedSecret(secret);
        if (!m_secrets->isSecretKnown(secret)) {
            settings.unreadSecretKeys.append(SecretStore::settingsKey(secret));
        }
        return value;
    };
    settings.refinement.cliproxyApiKey = read(SecretStore::Secret::CliproxyApiKey);
    settings.refinement.endpoint.apiKey =
        read(SecretStore::Secret::RefinementEndpointKey);
    settings.speech.endpoint.apiKey = read(SecretStore::Secret::SpeechEndpointKey);
    return settings;
}

bool SettingsStore::edit(const std::function<bool(AppSettings &)> &change)
{
    AppSettings draft = dictationSnapshot();
    const AppSettings before = draft;
    if (!change(draft)) {
        return false;
    }
    // Saving the rest would drop that change without a word.
    if (const QString field = changedUnstoredField(before, draft); !field.isEmpty()) {
        qCritical().noquote() << "Settings: not saved, since the edit changed " + field + ", which is never stored";
        return false;
    }
    applySnapshot(draft, before);
    return true;
}

void SettingsStore::applySnapshot(const AppSettings &draft)
{
    applySnapshot(draft, dictationSnapshot());
}

void SettingsStore::applySnapshot(const AppSettings &draft, const AppSettings &previous)
{
    if (m_settings.closed()) {
        qWarning().noquote() << "Settings: not saved, since removing Speecher deleted them";
        return;
    }
    using Secret = SecretStore::Secret;
    const std::tuple<Secret, QString, QString> keys[] = {
        {Secret::SpeechEndpointKey, draft.speech.endpoint.apiKey, previous.speech.endpoint.apiKey},
        {Secret::CliproxyApiKey, draft.refinement.cliproxyApiKey, previous.refinement.cliproxyApiKey},
        {Secret::RefinementEndpointKey, draft.refinement.endpoint.apiKey, previous.refinement.endpoint.apiKey},
    };
    QList<std::pair<Secret, QString>> secrets;
    for (const auto &[secret, value, before] : keys) {
        const QString key = SecretStore::settingsKey(secret);
        // An empty key the draft never read came from a skipped read, not
        // from the person. A key the edit left as it read it stays as it is:
        // saving it again would replace a field's save waiting its turn.
        const bool unchanged = draft.unreadSecretKeys.contains(key)
            ? value.trimmed().isEmpty()
            : !previous.unreadSecretKeys.contains(key) && value.trimmed() == before.trimmed();
        if (!unchanged) {
            secrets.append({secret, value});
        }
    }
    // First: a key cleared here is hidden before the settings saved with
    // it, such as a new server for it, can reach the file.
    m_secrets->saveSecrets(secrets);
    writeSnapshot(draft);
    reconcileLaunchAtLogin();
    const AppSettings saved = SettingsCodecs::snapshot();
    if (saved.audio != previous.audio) {
        emit audioCaptureSettingsChanged(saved.audio);
    }
    if (saved.updates != previous.updates) {
        emit updateSettingsChanged();
    }
    if (saved.correctionLearningEnabled != previous.correctionLearningEnabled) {
        emit correctionLearningEnabledChanged(saved.correctionLearningEnabled);
    }
    emit snapshotApplied(previous);
}

void SettingsStore::setLaunchAtLoginReconciler(LaunchAtLoginReconciler reconcile)
{
    m_reconcileLaunchAtLogin = std::move(reconcile);
}

void SettingsStore::reconcileLaunchAtLogin()
{
    if (!m_reconcileLaunchAtLogin) {
        return;
    }
    QString error;
    if (!m_reconcileLaunchAtLogin(SettingsCodecs::launchAtLogin(), &error)) {
        qWarning().noquote() << "launch at login reconciliation failed message=" + error;
    }
}

std::optional<QStringList> SettingsStore::addVocabularyTerms(const QStringList &terms)
{
    // It saves through a QSettings of its own, which would save this one's
    // changes too.
    if (m_settings.closed()) {
        return std::nullopt;
    }
    const std::optional<QStringList> held = SettingsCodecs::addVocabularyTerms(terms);
    if (held && held->size() < terms.size()) {
        emit vocabularyAdded();
    }
    return held;
}

bool SettingsStore::recordCorrectionEvidence(const CorrectionEvidence &evidence,
                                             const QString &applicationId)
{
    if (!SettingsCodecs::correctionLearningEnabled()) {
        return false;
    }
    if (!CorrectionSettingsCodec::recordEvidence(m_settings, evidence, applicationId)) {
        return false;
    }
    emit learnedCorrectionsChanged();
    return true;
}

bool SettingsStore::recordVocabularyUsage(const QString &text)
{
    if (!SettingsCodecs::recordVocabularyUsage(text)) {
        return false;
    }
    emit vocabularyUsageRecorded();
    return true;
}

SettingsFile &SettingsStore::raw()
{
    return m_settings;
}

} // namespace speecher
