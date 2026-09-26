#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <array>

namespace speecher {

class SettingsStore;

class SecretStore : public QObject {
    Q_OBJECT

public:
    // One keychain entry each. Every entry also has a key in the settings
    // file: the insecure fallback that holds the value when no keychain can.
    enum class Secret {
        OpenAiApiKey,
        CliproxyApiKey,
        SpeechEndpointKey,
        RefinementEndpointKey, // Last: sizes the cache.
    };

    explicit SecretStore(SettingsStore *settings, QObject *parent = nullptr);

    // Where the insecure fallback keeps a secret in the settings file.
    static QString settingsKey(Secret secret);

    // Reads the keyring entries Speecher wrote, other than the OpenAI key,
    // without waiting, so a dictation that starts later finds them in
    // memory. The OpenAI key keeps its deferred read (apiKey()).
    void prefetch();
    // Blocks on the keyring (1.5 s at most) unless the value is cached. A
    // failed read returns an empty string but is never cached as "not set".
    QString secret(Secret secret) const;
    // Never blocks: the cached value, or the settings-file copy of a secret
    // not in the keyring, else an empty string.
    QString cachedSecret(Secret secret) const;
    bool isSecretKnown(Secret secret) const;
    // Saves to the keychain, or to the settings file when no keychain works,
    // so a custom endpoint keeps working without one. An empty value for a
    // secret that was never read leaves the stored one alone: it came from a
    // skipped or failed read, not from the person.
    bool saveSecret(Secret secret, const QString &value);
    // Removes every Speecher entry from the keychain.
    bool deleteKeyringSecrets() const;

    QString apiKey() const;
    // Unlike saveSecret, refuses to fall back to the settings file.
    bool saveApiKey(const QString &apiKey);
    QString status() const;
    QString lastError() const;
    bool usesInsecureSettingsFallback() const;

private:
    struct CachedSecret {
        QString value;
        bool known = false;
        QElapsedTimer retryTimer;
    };

    CachedSecret &cached(Secret secret) const;
    void cacheValue(Secret secret, const QString &value) const;
    QString settingsFallback(Secret secret) const;
    bool deletionPending(Secret secret) const;
    bool mayBeInKeyring(Secret secret) const;
    void recordKeyringEntry(Secret secret, bool present) const;
    QString keyringSecret(Secret secret) const;
    bool writeKeyringSecret(Secret secret, const QString &value) const;
    bool deleteKeyringSecret(Secret secret) const;
    bool saveToKeyring(Secret secret, const QString &value);
    void migrateSettingsFallbacks();

    SettingsStore *m_settings;
    mutable QString m_lastError;
    mutable std::array<CachedSecret, size_t(Secret::RefinementEndpointKey) + 1> m_cache;
};

} // namespace speecher
