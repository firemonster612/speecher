#pragma once

#include "providers/SignInCheck.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

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
    // [{ id, name, state, detail }] for openai, anthropic and cliproxy. state
    // is signedIn, expired, missing or checking; a check keeps the last
    // detail until it answers.
    Q_PROPERTY(QVariantList services READ services NOTIFY servicesChanged)
    // [{ id, type, label, state }]: the file name, codex or claude, the
    // account it holds, and signedIn, expired, disabled or missing (chosen
    // but gone).
    Q_PROPERTY(QVariantList cliproxyAccounts READ cliproxyAccounts NOTIFY cliproxyAccountsChanged)
    // By secret row id: whether a value is saved. A secret the keyring could
    // not answer for is absent.
    Q_PROPERTY(QVariantMap savedSecrets READ savedSecrets NOTIFY secretsChanged)
    // By secret row id: why its last save or clear failed, the keyring's
    // words, possibly empty.
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
    QVariantMap secretErrors() const;

    // Checks the service's sign-in again on a worker, and for CLI Proxy API
    // reads its account files again. Only the latest check of a service
    // counts.
    Q_INVOKABLE void recheck(const QString &id);
    // Renews an expired sign-in with its refresh token, then checks it.
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

private:
    using SignInRun = SignInCheck (*)(SignInService, const RefinementSettings &, SecretStore *);

    void start();
    // One of services by id, or an empty map.
    QVariantMap service(const QString &id) const;
    void run(const QString &id, SignInRun signIn);
    void recheckAll();
    void readSavedSecrets();
    void readCliproxyAccounts();
    void finishSecretChange(const QString &rowId, bool done, bool saved);
    void settingsSaved();

    ApplicationController *m_controller;
    bool m_started = false;
    QHash<QString, SignInCheck> m_checks;
    // The check each service waits for, by its number.
    QHash<QString, quint64> m_pending;
    quint64 m_lastCheck = 0;
    QVariantList m_cliproxyAccounts;
    QVariantMap m_savedSecrets;
    QVariantMap m_secretErrors;
    // Secrets saved or cleared here since the read began, which it must not
    // overwrite.
    QSet<QString> m_secretsChanged;
    // The settings the sign-ins depend on, as last checked.
    QStringList m_signInSettings;
};

} // namespace speecher
