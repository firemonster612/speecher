#include "core/SecretStore.h"

#include "core/SettingsStore.h"
#include "core/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QDebug>
#include <QStandardPaths>
#include <QThread>

#include <iterator>
#include <memory>
#include <optional>

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

QString pendingDeletionKey(SecretStore::Secret secret)
{
    return SecretStore::settingsKey(secret) + QStringLiteral("PendingDeletion");
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
auto makeKeychainJob()
{
    return std::unique_ptr<Job, void (*)(Job *)>(new Job(keyringService()),
                                               [](Job *job) { job->deleteLater(); });
}

template <typename Job>
std::optional<QKeychain::Error> runKeychainJob(Job &job, QString *error)
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
    // QtKeychain's process-wide executor is not thread-safe. Its jobs run
    // asynchronously on the GUI thread; only this caller's thread waits.
    // Deferred deletion also keeps the job alive through executor cleanup.
    job.moveToThread(QCoreApplication::instance()->thread());
    job.start();
    if (!finished) {
        watchdog.start(keyringTimeoutMs);
        loop.exec();
    }
    if (!finished) {
        if (error) {
            *error = QStringLiteral("Desktop keyring request timed out");
        }
        return std::nullopt;
    }
    if (job.error() != QKeychain::NoError && error) {
        *error = job.errorString();
    }
    return job.error();
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
        if (row.secret == Secret::OpenAiApiKey || deletionPending(row.secret) || !mayBeInKeyring(row.secret) || cached(row.secret).known
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

bool SecretStore::deletionPending(Secret secret) const
{
    return m_settings && m_settings->raw().value(pendingDeletionKey(secret)).toBool();
}

bool SecretStore::isSecretKnown(Secret secret) const
{
    return deletionPending(secret) || cached(secret).known;
}

QString SecretStore::secret(Secret secret) const
{
    if (deletionPending(secret)) return {};
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
    if (deletionPending(secret)) return {};
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
        m_settings->raw().remove(pendingDeletionKey(secret));
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
    if (cleaned.isEmpty()) {
        // Suppress the old key across restarts until deletion can be retried.
        m_settings->raw().setValue(pendingDeletionKey(secret), true);
        m_settings->raw().remove(settingsKey(secret));
        m_settings->raw().sync();
        cacheValue(secret, {});
        return m_settings->raw().status() == QSettings::NoError;
    }
    m_settings->raw().remove(pendingDeletionKey(secret));
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
    if (QStandardPaths::isTestModeEnabled()) {
        const int delay = qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_READ_DELAY_MS");
        if (delay > 0) QThread::msleep(delay);
    }
#ifdef SPEECHER_WITH_QKEYCHAIN
    if (keyringReadsTimeOut()) {
        m_lastError = QStringLiteral("Desktop keyring request timed out");
        return {};
    }
    auto job = makeKeychainJob<QKeychain::ReadPasswordJob>();
    job->setKey(keyringEntry(secret));
    QString error;
    const auto result = runKeychainJob(*job, &error);
    if (result == QKeychain::NoError) {
        m_lastError.clear();
        return job->textData().trimmed();
    }
    if (result == QKeychain::EntryNotFound) {
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
    auto job = makeKeychainJob<QKeychain::WritePasswordJob>();
    job->setKey(keyringEntry(secret));
    job->setTextData(value);
    const bool ok = runKeychainJob(*job, &m_lastError) == QKeychain::NoError;
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
    if (QStandardPaths::isTestModeEnabled()
        && qEnvironmentVariableIntValue("SPEECHER_TEST_KEYRING_DELETE_FAILURE") == 1) {
        m_lastError = QStringLiteral("Keyring deletion denied");
        return false;
    }
#ifdef SPEECHER_WITH_QKEYCHAIN
    auto job = makeKeychainJob<QKeychain::DeletePasswordJob>();
    job->setKey(keyringEntry(secret));
    QString error;
    const auto result = runKeychainJob(*job, &error);
    if (result == QKeychain::NoError || result == QKeychain::EntryNotFound) {
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
        if (deletionPending(row.secret)) {
            if (deleteKeyringSecret(row.secret)) {
                m_settings->raw().remove(pendingDeletionKey(row.secret));
            }
            cacheValue(row.secret, {});
            continue;
        }
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
