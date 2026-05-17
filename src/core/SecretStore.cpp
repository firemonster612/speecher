#include "core/SecretStore.h"

#include "core/SettingsStore.h"

#ifdef SPEECHER_WITH_QKEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <keychain.h>
#endif

#include <QEventLoop>
#endif

namespace speecher {

namespace {

constexpr auto keyringService = "speecher";
constexpr auto openAiApiKeyEntry = "openai-api-key";

#ifdef SPEECHER_WITH_QKEYCHAIN
template <typename Job>
bool runKeychainJob(Job &job, QString *error)
{
    QEventLoop loop;
    job.setAutoDelete(false);
    QObject::connect(&job, &Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
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
    const QString key = keyringApiKey();
    if (!key.isEmpty()) {
        return key;
    }
    return m_settings ? m_settings->storedApiKeyFallback() : QString();
}

bool SecretStore::saveApiKey(const QString &apiKey)
{
    m_lastError.clear();
    const QString cleaned = apiKey.trimmed();
    const bool ok = cleaned.isEmpty() ? deleteKeyringApiKey() : writeKeyringApiKey(cleaned);
    if (ok && m_settings) {
        m_settings->clearStoredApiKeyFallback();
    }
    return ok;
}

QString SecretStore::status() const
{
#ifdef SPEECHER_WITH_QKEYCHAIN
    if (!apiKey().isEmpty()) {
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
    return runKeychainJob(job, &m_lastError);
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
