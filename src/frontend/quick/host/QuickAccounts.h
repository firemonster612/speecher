#pragma once

#include "providers/SignInCheck.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace speecher {

class ApplicationController;

// The UI's Accounts singleton: each service's sign-in as core finds it, CLI
// Proxy API's account files, and which secrets are saved. It saves and clears
// secrets through the daemon's SecretStore but never hands one to QML. Its
// wording is the UI's (Words, the Accounts page); a service's detail is
// core's status line.
class QuickAccounts final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Accounts)
    QML_SINGLETON
    // [{ id, name, state, found, detail, server, directory, inUse }] for
    // openai, anthropic and cliproxy. state is signedIn, expired, missing,
    // unknown (the keyring did not answer) or checking. found is the last
    // check's state, which the rest describe: detail is core's line, server
    // the CLI Proxy API server the sign-in goes through, directory the account
    // directory it reads, each possibly empty; they stay until the next check
    // answers. inUse says the settings sign in through it (signInInUse). The speech providers' sign-ins (Codex, Claude Voice) are
    // ProviderAvailability's, which Models shows; renewing a service checks
    // those again.
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    // [{ id, type, label, state }]: the file name, codex or claude, the
    // account it holds, and signedIn, expired, disabled, unusable (a file
    // dictation would refuse) or missing (chosen but gone).
    Q_PROPERTY(QVariantList cliproxyAccounts READ cliproxyAccounts NOTIFY cliproxyAccountsChanged)
    // By secret row id: whether a value is saved. A secret the keyring could
    // not answer for is absent.
    Q_PROPERTY(QVariantMap savedSecrets READ savedSecrets NOTIFY secretsChanged)
    // By secret row id: whether a clear the keyring refused is still to be
    // retried. The secret reads as not saved meanwhile, though the keyring
    // may still hold it.
    Q_PROPERTY(QVariantMap pendingClears READ pendingClears NOTIFY secretsChanged)
    // By secret row id: { action, reason } for its last save or clear that
    // failed. action is save or clear, reason the keyring's words, possibly
    // empty.
    Q_PROPERTY(QVariantMap secretErrors READ secretErrors NOTIFY secretsChanged)

public:
    explicit QuickAccounts(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickAccounts() override;

    // The engine's instance: the one the host made before loading any QML.
    // The first page to ask starts the checks, so a daemon whose window never
    // opens never reads the keyring for them.
    static QuickAccounts *create(QQmlEngine *, QJSEngine *);

    QVariantList services() const;
    QVariantList cliproxyAccounts() const;
    QVariantMap savedSecrets() const;
    QVariantMap pendingClears() const;
    QVariantMap secretErrors() const;

    // Checks the service's sign-in again on a worker, for CLI Proxy API with
    // its account files, and reads which secrets are saved again. Only the
    // latest check of a service counts.
    Q_INVOKABLE void recheck(const QString &id);
    // Renews an expired sign-in with its refresh token, then checks it and,
    // since renewing rewrites credentials they may share, the other services.
    Q_INVOKABLE void renew(const QString &id);
    // Saves a secret row's value (trimmed; an empty one changes nothing) to
    // the keychain, or for all but the OpenAI key to the settings file when
    // no keychain works, then checks the sign-ins again.
    Q_INVOKABLE void saveSecret(const QString &rowId, const QString &value);
    Q_INVOKABLE void clearSecret(const QString &rowId);

signals:
    void servicesChanged();
    void cliproxyAccountsChanged();
    void secretsChanged();
    // A save of the secret went through and no newer write of it waits: the
    // value asked for last is the one kept.
    void secretSaved(const QString &rowId);

private:
    using SignInRun = SignInCheck (*)(SignInService, AppSettings, SecretStore *);

    void start();
    // One of services by id, or an empty map.
    QVariantMap service(const QString &id) const;
    void run(const QString &id, SignInRun signIn);
    void recheckAll();
    // Asks ProviderAvailability for the speech providers' sign-ins, which
    // share credentials with these services.
    void checkSpeechSignIns();
    void readSavedSecrets();
    void showCliproxyAccounts(const QVariantList &accounts);
    // Saves value, or clears the secret when it is empty, once any write of
    // the same secret in progress is done.
    void writeSecret(const QString &rowId, const QString &value);
    void finishSecretChange(const QString &rowId, bool done, bool saved);
    void settingsSaved(const AppSettings &previous);

    ApplicationController *m_controller;
    bool m_started = false;
    QHash<QString, SignInCheck> m_checks;
    // The check each service waits for, by its number.
    QHash<QString, quint64> m_pending;
    quint64 m_lastCheck = 0;
    QVariantList m_cliproxyAccounts;
    QVariantMap m_savedSecrets;
    QVariantMap m_pendingClears;
    QVariantMap m_secretErrors;
    // The latest read of which secrets are saved; an earlier one's answer is
    // dropped.
    quint64 m_lastSecretRead = 0;
    // Secrets saved or cleared here since that read began, which it must not
    // overwrite.
    QSet<QString> m_secretsChanged;
    // Secrets being written, by row id, each with the value asked for while
    // it was (empty to clear), if any.
    QHash<QString, std::optional<QString>> m_writes;
    // The settings the sign-ins depend on, as last checked.
    QStringList m_signInSettings;
};

} // namespace speecher
