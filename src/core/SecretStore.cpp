#include "core/SecretStore.h"

#include "core/KeyringResult.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"

#include <QDebug>
#include <QStandardPaths>

#include <iterator>

#ifdef SPEECHER_WITH_QKEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <keychain.h>
#endif

#include <QEventLoop>
#include <QTimer>
#endif

namespace speecher {

namespace {

constexpr int keyringTimeoutMs = 1500;
constexpr int keyringRetryDelayMs = 5000;

struct SecretEntry {
    SecretStore::Secret secret;
    const char *keyringEntry;
    const QString &settingsKey;
};

// In enum order, one row per Secret.
const SecretEntry secretEntries[] = {
    {SecretStore::Secret::OpenAiApiKey, "openai-api-key", SettingsKeys::OpenAiApiKey},
    {SecretStore::Secret::CliproxyApiKey, "cliproxy-api-key", SettingsKeys::CliproxyApiKey},
    {SecretStore::Secret::SpeechEndpointKey, "speech-endpoint-key", SettingsKeys::SpeechEndpointApiKey},
    {SecretStore::Secret::RefinementEndpointKey, "refinement-endpoint-key", SettingsKeys::RefinementEndpointApiKey},
};
static_assert(std::size(secretEntries) == size_t(SecretStore::Secret::RefinementEndpointKey) + 1);

const SecretEntry &entryFor(SecretStore::Secret secret)
{
    return secretEntries[size_t(secret)];
}

QString keyringEntry(SecretStore::Secret secret)
{
    return QString::fromLatin1(entryFor(secret).keyringEntry);
}

// Test runs keep their entries apart from a real install's, the way test mode
// already keeps their settings file apart.
QString keyringService()
{
    return QStandardPaths::isTestModeEnabled() ? QStringLiteral("speecher-tests")
                                               : QStringLiteral("speecher");
}

// Tests stand in for a keyring that does not answer.
bool keyringReadsTimeOut()
{
    return QStandardPaths::isTestModeEnabled()
        && qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_READ_TIMEOUT") == 1;
}

} // namespace

QString SecretStore::settingsKey(Secret secret)
{
    return entryFor(secret).settingsKey;
}

namespace {

#ifdef SPEECHER_WITH_QKEYCHAIN
template <typename Job>
bool runKeychainJob(Job &job, QString *error)
{
    QEventLoop loop;
    QTimer watchdog;
    bool finished = false;
    job.setAutoDelete(false);
    QObject::connect(&job, &Job::finished, &loop, [&finished, &loop] {
        finished = true;
        loop.quit();
    });
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    job.start();
    if (!finished) {
        watchdog.start(keyringTimeoutMs);
        loop.exec();
    }
    if (!finished) {
        if (error) {
            *error = QStringLiteral("Desktop keyring request timed out");
        }
        return false;
    }
    if (job.error() == QKeychain::NoError) {
        return true;
    }
    if (error) {
        *error = job.errorString();
    }
    return false;
}
#endif

} // namespace

SecretStore::SecretStore(SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    migrateSettingsFallbacks();
}

SecretStore::CachedSecret &SecretStore::cached(Secret secret) const
{
    return m_cache[static_cast<size_t>(secret)];
}

void SecretStore::cacheValue(Secret secret, const QString &value) const
{
    CachedSecret &entry = cached(secret);
    entry.value = value;
    entry.known = true;
    entry.retryTimer.invalidate();
}

void SecretStore::prefetch()
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    for (const SecretEntry &row : secretEntries) {
        if (row.secret == Secret::OpenAiApiKey || !mayBeInKeyring(row.secret) || cached(row.secret).known
            || keyringReadsTimeOut()) {
            continue;
        }
        auto *job = new QKeychain::ReadPasswordJob(keyringService(), this);
        job->setKey(keyringEntry(row.secret));
        connect(job, &QKeychain::Job::finished, this, [this, job, secret = row.secret] {
            // A value saved or read meanwhile is newer than this answer.
            if (cached(secret).known) return;
            if (job->error() == QKeychain::NoError) {
                cacheValue(secret, job->textData().trimmed());
            } else if (job->error() == QKeychain::EntryNotFound) {
                cacheValue(secret, settingsFallback(secret));
            }
        });
        job->start();
    }
#endif
}

QString SecretStore::settingsFallback(Secret secret) const
{
    return m_settings ? m_settings->raw().value(settingsKey(secret)).toString().trimmed() : QString();
}

// Every install has read the OpenAI key from the keyring, so that entry is
// always looked up. The newer entries are read only once Speecher has written
// them, so a locked keyring is not woken for a key nobody saved.
bool SecretStore::mayBeInKeyring(Secret secret) const
{
    return secret == Secret::OpenAiApiKey
        || (m_settings
            && m_settings->raw().value(SettingsKeys::SecretsInKeyring).toStringList()
                   .contains(keyringEntry(secret)));
}

void SecretStore::recordKeyringEntry(Secret secret, bool present) const
{
    if (!m_settings || secret == Secret::OpenAiApiKey) {
        return;
    }
    QStringList entries = m_settings->raw().value(SettingsKeys::SecretsInKeyring).toStringList();
    entries.removeAll(keyringEntry(secret));
    if (present) {
        entries.append(keyringEntry(secret));
    }
    if (entries.isEmpty()) {
        m_settings->raw().remove(SettingsKeys::SecretsInKeyring);
    } else {
        m_settings->raw().setValue(SettingsKeys::SecretsInKeyring, entries);
    }
}

QString SecretStore::secret(Secret secret) const
{
    CachedSecret &entry = cached(secret);
    if (entry.known) {
        return entry.value;
    }
    if (entry.retryTimer.isValid() && !entry.retryTimer.hasExpired(keyringRetryDelayMs)) {
        return entry.value;
    }
    m_lastError.clear();
    entry.value = mayBeInKeyring(secret) ? keyringSecret(secret) : QString();
    if (entry.value.isEmpty()) {
        entry.value = settingsFallback(secret);
    }
    if (m_lastError.isEmpty()) {
        cacheValue(secret, entry.value);
    } else {
        entry.retryTimer.start();
    }
    return entry.value;
}

QString SecretStore::cachedSecret(Secret secret) const
{
    const CachedSecret &entry = cached(secret);
    if (entry.known || mayBeInKeyring(secret)) return entry.value;
    // Not in the keyring, so the settings file is the whole answer.
    cacheValue(secret, settingsFallback(secret));
    return entry.value;
}

bool SecretStore::saveToKeyring(Secret secret, const QString &value)
{
    m_lastError.clear();
    const QString cleaned = value.trimmed();
    CachedSecret &entry = cached(secret);
    if (entry.known && cleaned == entry.value && settingsFallback(secret).isEmpty()) {
        return true;
    }
    // Clearing a key that was never written to the keyring needs no keyring.
    const bool ok = cleaned.isEmpty()
        ? !mayBeInKeyring(secret) || deleteKeyringSecret(secret)
        : writeKeyringSecret(secret, cleaned);
    if (!ok) {
        return false;
    }
    if (m_settings) {
        m_settings->raw().remove(settingsKey(secret));
    }
    cacheValue(secret, cleaned);
    return true;
}

bool SecretStore::saveSecret(Secret secret, const QString &value)
{
    const QString cleaned = value.trimmed();
    const CachedSecret &entry = cached(secret);
    if (entry.known && cleaned == entry.value) {
        return true;
    }
    if (!entry.known && cleaned.isEmpty()) {
        return true;
    }
    if (saveToKeyring(secret, cleaned)) {
        return true;
    }
    if (!m_settings) {
        return false;
    }
    qWarning().noquote() << "keyring unavailable, keeping" << keyringEntry(secret)
                         << "in the settings file:" << m_lastError;
    m_settings->raw().setValue(settingsKey(secret), cleaned);
    cacheValue(secret, cleaned);
    return true;
}

QString SecretStore::apiKey() const
{
    return secret(Secret::OpenAiApiKey);
}

bool SecretStore::saveApiKey(const QString &apiKey)
{
    return saveToKeyring(Secret::OpenAiApiKey, apiKey);
}

QString SecretStore::status() const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    const CachedSecret &entry = cached(Secret::OpenAiApiKey);
    const QString key = entry.known ? entry.value : apiKey();
    if (!key.isEmpty()) {
        return usesInsecureSettingsFallback()
            ? QStringLiteral("API key saved in Speecher's settings file, not in the keyring")
            : QStringLiteral("API key saved in the desktop keyring");
    }
    if (!m_lastError.isEmpty()) {
        return QStringLiteral("The desktop keyring is not available: %1").arg(m_lastError);
    }
    return QStringLiteral("No API key saved");
#else
    return QStringLiteral("This build cannot save an API key in the desktop keyring");
#endif
}

QString SecretStore::lastError() const
{
    return m_lastError;
}

bool SecretStore::usesInsecureSettingsFallback() const
{
    return !settingsFallback(Secret::OpenAiApiKey).isEmpty();
}

QString SecretStore::keyringSecret(Secret secret) const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    if (keyringReadsTimeOut()) {
        m_lastError = QStringLiteral("Desktop keyring request timed out");
        return {};
    }
    QKeychain::ReadPasswordJob job(keyringService());
    job.setKey(keyringEntry(secret));
    QString error;
    if (runKeychainJob(job, &error)) {
        m_lastError.clear();
        return job.textData().trimmed();
    }
    if (job.error() == QKeychain::EntryNotFound) {
        m_lastError.clear();
        return {};
    }
    m_lastError = error;
#else
    Q_UNUSED(secret);
#endif
    return {};
}

bool SecretStore::writeKeyringSecret(Secret secret, const QString &value) const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    QKeychain::WritePasswordJob job(keyringService());
    job.setKey(keyringEntry(secret));
    job.setTextData(value);
    const bool ok = runKeychainJob(job, &m_lastError);
    if (ok) {
        recordKeyringEntry(secret, true);
    }
    return ok;
#else
    Q_UNUSED(secret);
    Q_UNUSED(value);
    m_lastError = QStringLiteral("QtKeychain support was not compiled in");
    return false;
#endif
}

bool SecretStore::deleteKeyringSecret(Secret secret) const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    QKeychain::DeletePasswordJob job(keyringService());
    job.setKey(keyringEntry(secret));
    QString error;
    runKeychainJob(job, &error);
    if (keyringDeletionSucceeded(job.error())) {
        m_lastError.clear();
        recordKeyringEntry(secret, false);
        return true;
    }
    m_lastError = error;
    return false;
#else
    recordKeyringEntry(secret, false);
    m_lastError.clear();
    return true;
#endif
}

bool SecretStore::deleteKeyringSecrets() const
{
    bool ok = true;
    QString firstError;
    for (const SecretEntry &row : secretEntries) {
        if (!deleteKeyringSecret(row.secret) && ok) {
            ok = false;
            firstError = m_lastError;
        }
    }
    m_lastError = firstError;
    m_cache = {};
    return ok;
}

// Keys an older Speecher (or a machine without a keyring) left in the
// settings file move into the keyring once one accepts them. CLI Proxy API
// keys were only ever stored there.
void SecretStore::migrateSettingsFallbacks()
{
    for (const SecretEntry &row : secretEntries) {
        const QString legacy = settingsFallback(row.secret);
        if (legacy.isEmpty()) {
            continue;
        }
        m_lastError.clear();
        if (writeKeyringSecret(row.secret, legacy)) {
            m_settings->raw().remove(row.settingsKey);
            // The value is in hand, so this run never needs to read it back.
            cacheValue(row.secret, legacy);
        }
    }
}

} // namespace speecher
