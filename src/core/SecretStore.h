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
    Q_ENUM(Secret)

    // What a save or clear tells its caller. The keyring's wait runs the
    // event loop, where the same secret can be saved again; the request asked
    // for last is the one kept, and only the call that settles a request
    // knows how it went.
    enum class SaveResult {
        // Storage holds what this call asked for.
        Stored,
        // Storage refused what this call asked for, and lastError() says
        // why. A refused clear still hides the key (deletionPending).
        Refused,
        // This call didn't settle its request: it came while another call
        // waited on the keyring for the same secret, which writes it next, or
        // a newer request came during this call's wait and this call wrote
        // that one instead. saveFinished says how the newest went, possibly
        // before this call returns.
        Pending,
    };
    Q_ENUM(SaveResult)

    explicit SecretStore(SettingsStore *settings, QObject *parent = nullptr);

    // Where the insecure fallback keeps a secret in the settings file.
    static QString settingsKey(Secret secret);

    // Reads the keyring entries Speecher wrote, other than the OpenAI key,
    // without waiting, so a dictation that starts later finds them in
    // memory. The OpenAI key keeps its deferred read (apiKey()).
    void prefetch();
    // Blocks on the keyring (1.5 s at most) unless the value is in memory. A
    // failed read returns an empty string but is never cached as "not set".
    // Every read, this and the ones below, returns a save's value from the
    // moment it is asked for, and a keyring answer that arrives after a
    // newer save is dropped.
    QString secret(Secret secret) const;
    // Never blocks: the value in memory, or the settings-file copy of a
    // secret not in the keyring, else an empty string.
    QString cachedSecret(Secret secret) const;
    bool isSecretKnown(Secret secret) const;
    // Whether a deletion the keyring refused waits to be retried; the secret
    // reads as empty meanwhile.
    bool deletionPending(Secret secret) const;
    // Asks for the secret to hold value, trimmed. An empty value clears it,
    // whether or not this store read it. The keychain takes it, or the
    // settings file when no keychain works, so a custom endpoint keeps
    // working without one. A key kept in the settings file is what every
    // reader finds until a keyring takes it. A deletion the keyring refuses
    // leaves a marker in the settings file that hides the key until a
    // deletion goes through.
    SaveResult saveSecret(Secret secret, const QString &value);
    // saveSecret for each, all asked for before the first keyring wait, so a
    // save made during one is newer and stands. A key the keyring refused is
    // logged with why.
    void saveSecrets(const QList<std::pair<Secret, QString>> &values);
    // Removes every Speecher entry from the keychain.
    bool deleteKeyringSecrets() const;

    QString apiKey() const;
    // Unlike saveSecret, refuses to fall back to the settings file.
    SaveResult saveApiKey(const QString &apiKey);
    QString status() const;
    QString lastError() const;
    // Why the last read of the secret failed, naming the secret, or nothing
    // when it was read. Ask right after the read: it carries lastError().
    QString readFailure(Secret secret) const;
    bool usesInsecureSettingsFallback() const;

signals:
    // A save or clear of the secret settled: storage holds the one asked for
    // last, or refused it. cleared says it was a clear.
    void saveFinished(SecretStore::Secret secret, bool cleared, bool stored);

private:
    // Startup migration belongs to the controller, not temporary settings stores.
    friend class ApplicationController;

    // A save, or a clear when value is empty.
    struct Request {
        QString value;
        // Whether the settings file keeps the value when the keyring refuses it.
        bool fallBack = true;
    };

    struct SecretState {
        // What storage holds, as this store last read or wrote it.
        std::optional<QString> stored;
        // Started by a failed keyring read, which isn't retried until it expires.
        QElapsedTimer retryTimer;
        // The save asked for last, until it settles. Reads return it.
        std::optional<Request> requested;
        // Counts the saves asked for, so a write or read that waited on the
        // keyring can tell whether one came meanwhile.
        quint64 requests = 0;
        // Whether a keyring write of the secret is under way.
        bool writing = false;
    };

    SecretState &state(Secret secret) const;
    void cacheValue(Secret secret, const QString &value) const;
    std::optional<QString> readWithoutKeyring(Secret secret) const;
    QString settingsFallback(Secret secret) const;
    bool mayBeInKeyring(Secret secret) const;
    void recordKeyringEntry(Secret secret, bool present) const;
    QString keyringSecret(Secret secret) const;
    bool writeKeyringSecret(Secret secret, const QString &value) const;
    bool deleteKeyringSecret(Secret secret) const;
    quint64 ask(Secret secret, const Request &request);
    SaveResult save(Secret secret, const Request &request);
    SaveResult settle(Secret secret, quint64 own);
    bool holds(Secret secret, const Request &request) const;
    bool write(Secret secret, const Request &request);
    void migrateSettingsFallbacks();

    SettingsStore *m_settings;
    mutable QString m_lastError;
    mutable std::array<SecretState, size_t(Secret::RefinementEndpointKey) + 1> m_cache;
};

} // namespace speecher
