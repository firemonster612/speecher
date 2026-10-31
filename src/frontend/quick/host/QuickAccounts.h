#pragma once

#include "frontend/quick/host/HostSingleton.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace speecher {

class Accounts;
class ApplicationController;
struct AppSettings;

// The UI's Accounts singleton: the daemon's Accounts as QML values, by the
// schema's row ids. It saves and clears secrets through them but never hands
// one to QML. Its wording is the UI's (Words, the Accounts page); a service's
// detail is core's status line.
class QuickAccounts final : public QObject, public HostSingleton<QuickAccounts> {
    Q_OBJECT
    QML_NAMED_ELEMENT(Accounts)
    QML_SINGLETON
    // [{ id, name, state, found, detail, server, directory, inUse }] for
    // openai, anthropic and cliproxy. state is signedIn, expired, missing,
    // unknown (the keyring did not answer) or checking. found is the last
    // check's state, which the rest describe: detail is core's line, server
    // the CLI Proxy API server the sign-in goes through, directory the account
    // directory it reads, each possibly empty; they stay until the next check
    // answers. inUse says the settings sign in through it (signInInUse).
    // The speech providers' sign-ins (Codex, Claude Voice) are
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
    // The names Settings.values holds for each auth mode, for QML to compare
    // against: { openAi: { auto, codexOauth, codexApiKey, env, settings,
    // cliproxy }, anthropic: { oauth, cliproxy } }.
    Q_PROPERTY(QVariantMap authModes READ authModes CONSTANT)

public:
    explicit QuickAccounts(ApplicationController *controller, QObject *parent = nullptr);

    // The engine's instance: the one the host made before loading any QML.
    // The first page to ask starts the checks, so a daemon whose window never
    // opens never reads the keyring for them.
    static QuickAccounts *create(QQmlEngine *, QJSEngine *);

    QVariantList services() const;
    QVariantList cliproxyAccounts() const;
    QVariantMap savedSecrets() const;
    QVariantMap pendingClears() const;
    QVariantMap secretErrors() const;
    QVariantMap authModes() const;

    // Accounts::recheck, renew, saveSecret and clearSecret by id.
    Q_INVOKABLE void recheck(const QString &id);
    Q_INVOKABLE void renew(const QString &id);
    Q_INVOKABLE void saveSecret(const QString &rowId, const QString &value);
    Q_INVOKABLE void clearSecret(const QString &rowId);

signals:
    void servicesChanged();
    void cliproxyAccountsChanged();
    void secretsChanged();
    // The row's field is done with the value it submitted (Accounts::draftSettled).
    void draftSettled(const QString &rowId);

private:
    // One of services by id, or an empty map.
    QVariantMap service(const QString &id) const;
    void settingsSaved(const AppSettings &previous);

    ApplicationController *m_controller;
    Accounts *m_accounts;
};

} // namespace speecher
