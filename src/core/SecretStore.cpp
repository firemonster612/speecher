#include "core/SecretStore.h"

#include "core/KeyringResult.h"
#include "core/SettingsStore.h"

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

constexpr auto keyringService = "speecher";
constexpr auto openAiApiKeyEntry = "openai-api-key";
constexpr int keyringTimeoutMs = 1500;
constexpr int keyringRetryDelayMs = 5000;

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
    migrateLegacySettingsKey();
}

QString SecretStore::apiKey() const
{
    if (m_hasApiKeyResult) {
        return m_lastApiKey;
    }
    if (m_apiKeyRetryTimer.isValid()
        && !m_apiKeyRetryTimer.hasExpired(keyringRetryDelayMs)) {
        return m_lastApiKey;
    }
    m_lastApiKey = keyringApiKey();
    if (m_lastApiKey.isEmpty() && m_settings) {
        m_lastApiKey = m_settings->storedApiKeyFallback();
    }
    m_hasApiKeyResult = m_lastError.isEmpty();
    if (m_hasApiKeyResult) {
        m_apiKeyRetryTimer.invalidate();
    } else {
        m_apiKeyRetryTimer.start();
    }
    return m_lastApiKey;
}

bool SecretStore::saveApiKey(const QString &apiKey)
{
    m_lastError.clear();
    const QString cleaned = apiKey.trimmed();
    if (m_hasApiKeyResult && cleaned == m_lastApiKey
        && !usesInsecureSettingsFallback()) {
        return true;
    }
    const bool ok = cleaned.isEmpty() ? deleteKeyringApiKey() : writeKeyringApiKey(cleaned);
    if (ok && m_settings) {
        m_settings->clearStoredApiKeyFallback();
    }
    if (ok) {
        m_lastApiKey = cleaned;
        m_hasApiKeyResult = true;
        m_apiKeyRetryTimer.invalidate();
    }
    return ok;
}

QString SecretStore::status() const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    const QString key = m_hasApiKeyResult ? m_lastApiKey : apiKey();
    if (!key.isEmpty()) {
        return usesInsecureSettingsFallback() ? QStringLiteral("Settings API key found in legacy plaintext settings")
                                             : QStringLiteral("Settings API key stored in desktop keyring");
    }
    if (!m_lastError.isEmpty()) {
        return QStringLiteral("Desktop keyring unavailable: %1").arg(m_lastError);
    }
    return QStringLiteral("No app settings API key found");
#else
    return QStringLiteral("QtKeychain support was not compiled in");
#endif
}

QString SecretStore::lastError() const
{
    return m_lastError;
}

bool SecretStore::usesInsecureSettingsFallback() const
{
    return m_settings && !m_settings->storedApiKeyFallback().isEmpty();
}

QString SecretStore::keyringApiKey() const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    QKeychain::ReadPasswordJob job(QString::fromLatin1(keyringService));
    job.setKey(QString::fromLatin1(openAiApiKeyEntry));
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
#endif
    return {};
}

bool SecretStore::writeKeyringApiKey(const QString &apiKey) const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    QKeychain::WritePasswordJob job(QString::fromLatin1(keyringService));
    job.setKey(QString::fromLatin1(openAiApiKeyEntry));
    job.setTextData(apiKey);
    return runKeychainJob(job, &m_lastError);
#else
    m_lastError = QStringLiteral("QtKeychain support was not compiled in");
    return false;
#endif
}

bool SecretStore::deleteKeyringApiKey() const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    QKeychain::DeletePasswordJob job(QString::fromLatin1(keyringService));
    job.setKey(QString::fromLatin1(openAiApiKeyEntry));
    QString error;
    runKeychainJob(job, &error);
    if (keyringDeletionSucceeded(job.error())) {
        m_lastError.clear();
        return true;
    }
    m_lastError = error;
    return false;
#else
    m_lastError = QStringLiteral("QtKeychain support was not compiled in");
    return false;
#endif
}

void SecretStore::migrateLegacySettingsKey()
{
    if (!m_settings) {
        return;
    }
    const QString legacyKey = m_settings->storedApiKeyFallback().trimmed();
    if (legacyKey.isEmpty()) {
        return;
    }
    m_lastError.clear();
    if (writeKeyringApiKey(legacyKey)) {
        m_settings->clearStoredApiKeyFallback();
    }
}

} // namespace speecher
