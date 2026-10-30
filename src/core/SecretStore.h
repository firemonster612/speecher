#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include <array>
#include <optional>
#include <utility>

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
    // Whether a deletion the keyring refused waits to be retried; the secret
    // reads as empty meanwhile.
    bool deletionPending(Secret secret) const;
    // Saves to the keychain, or to the settings file when no keychain works,
    // so a custom endpoint keeps working without one. A key kept in the
    // settings file is what every read finds until a keyring takes it. An
    // empty value for a secret that was never read leaves the stored one
    // alone: it came from a skipped or failed read, not from the person.
    // Failed deletions leave a persisted marker that suppresses reads until
    // deletion succeeds.
    //
    // A save that waits for the keyring runs the event loop, where the same
    // secret can be saved again. That save waits for the one in flight and
    // then applies, so the value asked for last is the one kept; it returns
    // true at once, and the save in flight returns how the last one went.
    // Meanwhile every read returns the value asked for last, and a save of
    // that same value asks for nothing new. The same holds for clearSecret
    // and saveApiKey.
    bool saveSecret(Secret secret, const QString &value);
    // saveSecret for each, in order. A save that waits for the keyring runs
    // the event loop, where an edit can save one of these secrets too; that
    // save is newer, so this one leaves that secret alone.
    void saveSecrets(const QList<std::pair<Secret, QString>> &values);
    // Removes a secret whether or not it was read, the way saveSecret clears
    // one it has read. False when the keyring refused: the marker then hides
    // the key until a later deletion succeeds, and lastError() says why.
    bool clearSecret(Secret secret);
    // Removes every Speecher entry from the keychain.
    bool deleteKeyringSecrets() const;

    QString apiKey() const;
    // Unlike saveSecret, refuses to fall back to the settings file.
    bool saveApiKey(const QString &apiKey);
    QString status() const;
    QString lastError() const;
    // Why the last read of the secret failed, naming the secret, or nothing
    // when it was read. Ask right after the read: it carries lastError().
    QString readFailure(Secret secret) const;
    bool usesInsecureSettingsFallback() const;

private:
    // Startup migration belongs to the controller, not temporary settings stores.
    friend class ApplicationController;

    struct SaveRequest {
        QString value;
        // Whether the settings file keeps the value when the keyring refuses it.
        bool fallBack = true;
    };

    struct CachedSecret {
        QString value;
        bool known = false;
        QElapsedTimer retryTimer;
        // Counts the saves that asked for a new value, so saveSecrets can tell
        // whether one came meanwhile.
        unsigned saves = 0;
        // While a save is in flight: the value it writes, and the latest save
        // asked for meanwhile, which goes next.
        std::optional<QString> writing;
        std::optional<SaveRequest> queued;

        // The value asked for last, while a save is in flight.
        std::optional<QString> asked() const
        {
            return queued ? queued->value : writing;
        }
    };

    CachedSecret &cached(Secret secret) const;
    void cacheValue(Secret secret, const QString &value) const;
    QString settingsFallback(Secret secret) const;
    bool mayBeInKeyring(Secret secret) const;
    void recordKeyringEntry(Secret secret, bool present) const;
    QString keyringSecret(Secret secret) const;
    bool writeKeyringSecret(Secret secret, const QString &value) const;
    bool deleteKeyringSecret(Secret secret) const;
    bool save(Secret secret, const SaveRequest &request);
    bool saveToKeyring(Secret secret, const QString &cleaned);
    bool storeSecret(Secret secret, const QString &cleaned);
    void migrateSettingsFallbacks();

    SettingsStore *m_settings;
    mutable QString m_lastError;
    mutable std::array<CachedSecret, size_t(Secret::RefinementEndpointKey) + 1> m_cache;
};

} // namespace speecher
