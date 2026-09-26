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
        RefinementEndpointKey,
    };

    explicit SecretStore(SettingsStore *settings, QObject *parent = nullptr);

    // Where the insecure fallback keeps a secret in the settings file.
    static QString settingsKey(Secret secret);

    QString secret(Secret secret) const;
    // Saves to the keychain, or to the settings file when no keychain works,
    // so a custom endpoint keeps working without one.
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
    QString settingsFallback(Secret secret) const;
    bool mayBeInKeyring(Secret secret) const;
    void recordKeyringEntry(Secret secret, bool present) const;
    QString keyringSecret(Secret secret) const;
    bool writeKeyringSecret(Secret secret, const QString &value) const;
    bool deleteKeyringSecret(Secret secret) const;
    bool saveToKeyring(Secret secret, const QString &value);
    void migrateSettingsFallbacks();

    SettingsStore *m_settings;
    mutable QString m_lastError;
    mutable std::array<CachedSecret, 4> m_cache;
};

} // namespace speecher
