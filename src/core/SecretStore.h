#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace speecher {

class SettingsStore;

// Speecher's API keys: in the desktop keyring, or in the settings file when
// no keyring takes one. Only one store per settings file saves keys, the
// running app's: every store on the file sees another's changes before the
// file saves them (QSettings keeps one copy of an open file), so a second
// store that saved a key it read that way could report a key stored that the
// file never saved. Only that store's own SettingsFile deletes the settings
// (SettingsStore::deleteAll): another handle's deletion would leave this
// store reading copies it cached of a file that is gone.
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

    explicit SecretStore(SettingsStore *settings, QObject *parent = nullptr);

    // Where the insecure fallback keeps a secret in the settings file.
    static QString settingsKey(Secret secret);

    // Starts reading the keyring entries Speecher wrote, other than the
    // OpenAI key, so a dictation that starts later finds them in memory. The
    // OpenAI key keeps its deferred read (apiKey()).
    void prefetch();
    // The value in memory, or the settings-file copy of a secret not in the
    // keyring, else the keyring's. On the main thread, whose event loop
    // brings the keyring's answers, this never waits: the secret stays
    // unread (isSecretKnown) until the read it starts answers. Elsewhere it
    // waits for the keyring (1.5 s at most). A failed read returns an empty
    // string, is never cached as "not set", and isn't tried again for 5 s.
    // Every read, this and the ones below, returns the value asked for last
    // from the moment it is asked for.
    QString secret(Secret secret);
    // Never reads the keyring: the value in memory, or the settings-file copy
    // of a secret not in the keyring, else an empty string.
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
    // deletion goes through. Returns the request's number, which
    // saveFinished carries once storage answers, never during this call. One
    // secret is written at a time; a request made meanwhile waits its turn,
    // replacing any other waiting one, and answers for the one being written
    // too.
    quint64 saveSecret(Secret secret, const QString &value);
    // What a settings edit saves: the secrets it changed (saveSecret). First
    // it saves the settings file for each other secret whose change the file
    // refused to save before, and reports the ones that saved as stored.
    void saveSecrets(const QList<std::pair<Secret, QString>> &values);
    // Removes every Speecher entry from the keychain, after any write the
    // keychain is taking. Every save waiting its turn is refused, the answer
    // of a write under way is dropped, and saves asked for before done runs
    // are refused. A deleted entry's deletion marker goes, having nothing
    // left to hide; an OpenAI key the keyring refuses to delete gets one,
    // unless a read then finds the entry gone. The settings file's copies
    // stay: SettingsStore::deleteAll removes them, after which every save is
    // refused. done gets whether every entry went, else the first refusal's
    // reason; a removal asked for while one runs finishes with it.
    void deleteKeyringSecrets(std::function<void(bool deleted, const QString &error)> done);
    // Calls done once no save, clear or removal asked for is still under way
    // or waiting its turn, during this call when none is, replacing a done
    // given before. Every keyring request answers within 1.5 s, so it comes.
    // Quitting waits on it, so what was asked for reaches storage before the
    // event loop stops.
    void afterWrites(std::function<void()> done);
    // The settings keys of the deletion markers (deletionPending) in the
    // settings file, each hiding a key the keyring kept.
    QStringList deletionMarkers() const;

    QString apiKey();
    // Unlike saveSecret, refuses to fall back to the settings file.
    quint64 saveApiKey(const QString &apiKey);
    QString status();
    QString lastError() const;
    // Why the last read of the secret failed, naming the secret, or nothing
    // when it was read. Ask right after the read, or its answer on the main
    // thread: it carries lastError().
    QString readFailure(Secret secret) const;
    bool usesInsecureSettingsFallback() const;

signals:
    // The newest save or clear of the secret, number request (saveSecret),
    // settled: storage holds it, or refused it for error's reason. cleared
    // says it was a clear. A request a newer one replaced, while it waited
    // its turn or before the keyring answered it, has no saveFinished of its
    // own. Also comes, as request 0, when an earlier change of the secret
    // that the settings file didn't save reaches it (saveSecrets).
    void saveFinished(SecretStore::Secret secret, quint64 request, bool cleared, bool stored, const QString &error);

private:
    // Startup migration belongs to the controller, not temporary settings stores.
    friend class ApplicationController;

    // A save, or a clear when value is empty.
    struct Request {
        QString value;
        // Whether the settings file keeps the value when the keyring refuses it.
        bool fallBack = true;
        // A startup migration's: written though storage holds it, and
        // reporting nothing (migrateSettingsFallbacks).
        bool migrating = false;
        // Whether a deletion marker hid the key when it was asked for, other
        // than one a request still under way put in (hide).
        bool marked = false;
        quint64 number = 0;
    };

    // Where a secret's writes stand. One is written at a time, and each event
    // has one handler:
    //   from     event                to
    //   Idle     ask (save)           Writing it
    //   Writing  ask (save)           it waits its turn, replacing any waiting
    //   Writing  answer (finishWrite) the request stored, then Writing the
    //                                 waiting one, which answers for it, else
    //                                 Idle and saveFinished
    //   any      removal, or an ask   Dropped (Idle stays Idle), nothing
    //            refused (drop)       waiting; saveFinished refuses the newest
    //   Dropped  answer (finishWrite) Writing the waiting one, else Idle;
    //                                 nothing stored
    enum class Phase { Idle, Writing, Dropped };

    struct SecretState {
        // What storage holds, as this store last read or wrote it.
        std::optional<QString> stored;
        // Started by a failed keyring read, which isn't retried until it expires.
        QElapsedTimer retryTimer;
        // Whether the main thread's keyring read is under way.
        bool reading = false;
        Phase phase = Phase::Idle;
        // The request being written, while Writing, and the one waiting its turn.
        Request writing;
        std::optional<Request> waiting;
        // The count of settings file saves (SettingsFile::saves) at which the
        // file holds its last change of what it says about the secret:
        // nothing of that change counts as stored before.
        quint64 savedBy = 0;
        // Whether the last answer refused a request storage took, only for
        // the settings file not saving it (saveSecrets).
        bool commitRefused = false;

        // The request asked for last, waiting or being written, whose value
        // reads return; null when none is.
        const Request *newest() const;
        // Leaves the keyring's answer to the request being written unheard,
        // and nothing waiting. Returns the newest request it dropped.
        std::optional<Request> drop();
    };

    // What the settings file tells a reader about a secret: the copy it keeps,
    // whether a deletion marker hides the key, and whether the keyring may
    // hold it (mayBeInKeyring).
    struct FileSays {
        QString copy;
        bool hidden = false;
        bool inKeyring = false;
        bool operator==(const FileSays &) const = default;
        // The same, but the marker hides the key and no copy is kept.
        FileSays withKeyHidden() const { return {{}, true, inKeyring}; }
    };

    // What a keyring request found: whether it went through, the entry's
    // value for a read, and why not otherwise. A read or deletion of a
    // missing entry goes through, a read with nothing.
    struct KeyringAnswer {
        bool ok = false;
        QString value;
        QString error;
    };
    enum class KeyringRequest { Read, Write, Delete };
    using KeyringDone = std::function<void(const KeyringAnswer &)>;

    // Starts the request on the secret's keyring entry; call it on the main
    // thread. done runs on context's thread with the keyring's answer, or a
    // timeout after 1.5 s, and never during this call.
    static void askKeyring(const QObject *context, KeyringRequest request, Secret secret, const QString &value,
                           KeyringDone done);
    static KeyringAnswer waitForKeyring(Secret secret);
    void deleteKeyringEntry(Secret secret, KeyringDone done);

    SecretState &state(Secret secret) const;
    void cacheValue(Secret secret, const QString &value) const;
    std::optional<QString> readWithoutKeyring(Secret secret) const;
    void startRead(Secret secret);
    void finishRead(Secret secret, const KeyringAnswer &answer);
    QString settingsFallback(Secret secret) const;
    bool mayBeInKeyring(Secret secret) const;
    void recordKeyringEntry(Secret secret, bool present) const;
    QString writeRefusal() const;
    quint64 save(Secret secret, Request request);
    void writeNext(Secret secret);
    FileSays fileSays(Secret secret) const;
    bool say(Secret secret, const FileSays &says);
    void hide(Secret secret);
    void finishWrite(Secret secret, const std::optional<KeyringAnswer> &answer);
    bool writesSettled() const;
    void notifyIfWritesSettled();
    bool store(Secret secret, const Request &request, const std::optional<KeyringAnswer> &answer);
    void settle(Secret secret, const Request &request, bool stored, const QString &error);
    bool holds(Secret secret, const Request &request) const;
    bool keep(Secret secret, const Request &request, const KeyringAnswer &answer, FileSays &file);
    void changedInFile(Secret secret);
    bool commit(Secret secret);
    void removeNext(size_t index);
    void finishRemoval();
    void migrateSettingsFallbacks();

    SettingsStore *m_settings;
    QString m_lastError;
    // Numbers every request.
    quint64 m_requests = 0;
    // While deleteKeyringSecrets runs, what each of its callers waits for;
    // empty otherwise.
    QList<std::function<void(bool, const QString &)>> m_removals;
    // The first refusal the removal under way met.
    std::optional<QString> m_removalError;
    // What afterWrites calls once the writes settle.
    std::function<void()> m_afterWrites;
    mutable std::array<SecretState, size_t(Secret::RefinementEndpointKey) + 1> m_cache;
};

} // namespace speecher
