#pragma once

// Waits on SecretStore's answers, which come through the event loop once the
// keyring answers.

#include "core/SecretStore.h"

#include <QTest>

#include <memory>
#include <optional>

namespace speecher {

// Runs ask, which saves or clears the secret, and waits for the answer to
// it (SecretStore::saveFinished): whether storage holds it.
template <typename Ask>
bool storedAfter(SecretStore *secrets, SecretStore::Secret secret, Ask ask)
{
    std::optional<bool> stored;
    QObject receiver;
    QObject::connect(secrets, &SecretStore::saveFinished, &receiver,
                     [&stored, secret](SecretStore::Secret finished, quint64, bool, bool ok) {
        if (finished == secret) stored = ok;
    });
    ask();
    return QTest::qWaitFor([&stored] { return stored.has_value(); }, 5000) && *stored;
}

inline bool savedSecret(SecretStore *secrets, SecretStore::Secret secret, const QString &value)
{
    return storedAfter(secrets, secret, [&] { secrets->saveSecret(secret, value); });
}

inline bool savedApiKey(SecretStore *secrets, const QString &value)
{
    return storedAfter(secrets, SecretStore::Secret::OpenAiApiKey, [&] { secrets->saveApiKey(value); });
}

// SecretStore::deleteKeyringSecrets, waited for: whether every entry went.
inline bool removedSecrets(SecretStore *secrets)
{
    const auto removed = std::make_shared<std::optional<bool>>();
    secrets->deleteKeyringSecrets([removed](bool deleted, const QString &) { *removed = deleted; });
    return QTest::qWaitFor([removed] { return removed->has_value(); }, 10000) && **removed;
}

// SecretStore::secret, waiting for the keyring read it starts on the main
// thread.
inline QString readSecret(SecretStore *secrets, SecretStore::Secret secret)
{
    secrets->secret(secret);
    const bool answered = QTest::qWaitFor(
        [&] { return secrets->isSecretKnown(secret) || !secrets->readFailure(secret).isEmpty(); }, 5000);
    return answered ? secrets->secret(secret) : QStringLiteral("<keyring never answered>");
}

} // namespace speecher
