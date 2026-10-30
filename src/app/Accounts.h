#pragma once

#include "core/SecretStore.h"
#include "providers/CliProxyCredentials.h"
#include "providers/SignInCheck.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QStringList>

#include <optional>

namespace speecher {

class ProviderAvailability;
class ProviderRegistry;
class SettingsStore;

// The daemon's accounts: each service's sign-in as its latest check found it,
// CLI Proxy API's account files, and which secrets are saved. Checks and
// secret reads run on workers, so the keyring and credential files never
// block the GUI thread; what a check definitely finds of a provider's sign-in
// is noted in ProviderAvailability. Nothing is read until start().
class Accounts final : public QObject {
    Q_OBJECT

public:
    // One of CLI Proxy API's account files and the login it holds, codex or
    // claude.
    struct CliProxyAccountFile {
        QString type;
        CliProxyAccount account;
        bool operator==(const CliProxyAccountFile &) const = default;
    };
    // Why a save or clear of a secret did not go through.
    struct WriteFailure {
        bool clearing = false;
        // The keyring's words, possibly empty.
        QString reason;
    };

    Accounts(SettingsStore &settings,
             ProviderAvailability &availability,
             ProviderRegistry &registry,
             QObject *parent = nullptr);

    // Checks every sign-in, reads which secrets are saved, and from then on
    // checks the sign-ins again whenever a setting they read is saved or a
    // secret is written. Later calls do nothing.
    void start();

    // The service's latest answered check, or nothing before one answers.
    std::optional<SignInCheck> signIn(SignInService service) const;
    // Whether a check of the service is on its way; its answer replaces
    // signIn's.
    bool checking(SignInService service) const { return m_pending.contains(service); }
    // As CLI Proxy API's latest check read them.
    const QList<CliProxyAccountFile> &cliproxyAccounts() const { return m_cliproxyAccounts; }
    // By secret: whether a value is saved. A secret the keyring has not
    // answered for is absent.
    const QMap<SecretStore::Secret, bool> &savedSecrets() const { return m_savedSecrets; }
    // By secret: whether a clear the keyring refused is still to be retried.
    // The secret reads as not saved meanwhile, though the keyring may still
    // hold it. Absent like savedSecrets.
    const QMap<SecretStore::Secret, bool> &pendingClears() const { return m_pendingClears; }
    // By secret: why its last save or clear failed, until one succeeds.
    const QMap<SecretStore::Secret, WriteFailure> &writeFailures() const { return m_writeFailures; }

    // Checks the service's sign-in again, for CLI Proxy API with its account
    // files, and reads which secrets are saved again. Only the latest check
    // of a service counts.
    void recheck(SignInService service);
    // Renews an expired sign-in with its refresh token, then checks it and,
    // since renewing rewrites credentials they may share, the other services
    // and the speech providers' sign-ins.
    void renew(SignInService service);
    // Saves a secret (trimmed; an empty value changes nothing) to the
    // keychain, or for all but the OpenAI key to the settings file when no
    // keychain works, then checks the sign-ins again.
    void saveSecret(SecretStore::Secret secret, const QString &value);
    void clearSecret(SecretStore::Secret secret);

signals:
    // A service's check started or answered.
    void signInsChanged();
    void cliproxyAccountsChanged();
    void secretsChanged();
    // A save of the secret went through and no newer write of it waits: the
    // value asked for last is the one kept.
    void secretSaved(SecretStore::Secret secret);

private:
    using SignInRun = SignInCheck (*)(SignInService, AppSettings, SecretStore *);

    void run(SignInService service, SignInRun signIn);
    void recheckAll();
    void readSavedSecrets();
    // Saves value, or clears the secret when it is empty. The answer comes
    // with SecretStore::saveFinished, during this call or after a write of
    // the same secret under way, carrying the number the secret waits for.
    void write(SecretStore::Secret secret, const QString &value);
    void secretSaveFinished(SecretStore::Secret secret, quint64 request, bool cleared, bool stored,
                            const QString &error);
    void finishWrite(SecretStore::Secret secret, bool done, bool saved, const QString &error);
    void settingsSaved();

    SettingsStore &m_settings;
    ProviderAvailability &m_availability;
    ProviderRegistry &m_registry;
    bool m_started = false;
    QMap<SignInService, SignInCheck> m_checks;
    // The check each service waits for, by its number.
    QMap<SignInService, quint64> m_pending;
    quint64 m_lastCheck = 0;
    QList<CliProxyAccountFile> m_cliproxyAccounts;
    QMap<SecretStore::Secret, bool> m_savedSecrets;
    QMap<SecretStore::Secret, bool> m_pendingClears;
    QMap<SecretStore::Secret, WriteFailure> m_writeFailures;
    // The latest read of which secrets are saved; an earlier one's answer is
    // dropped.
    quint64 m_lastSecretRead = 0;
    // Secrets saved or cleared since that read began, which it must not
    // overwrite.
    QList<SecretStore::Secret> m_secretsWritten;
    // Secrets whose save or clear waits for the store's answer, with the
    // number of the request each asked for last (SecretStore::nextRequest).
    QMap<SecretStore::Secret, quint64> m_saving;
    // The settings the sign-ins depend on, as last checked.
    QStringList m_signInSettings;
};

} // namespace speecher
