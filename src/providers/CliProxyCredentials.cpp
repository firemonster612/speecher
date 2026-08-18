#include "providers/CliProxyCredentials.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace speecher {
namespace {

QJsonObject readAccountObject(const QString &directory, const QString &fileName)
{
    QFile file(QDir(directory).filePath(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString accountLabel(const QString &fileName, const QString &type)
{
    QString label = fileName;
    label.remove(0, type.size() + 1);
    label.chop(QStringLiteral(".json").size());
    return label;
}

bool accountExpired(const QJsonObject &account)
{
    const QDateTime expiry = QDateTime::fromString(account.value(QStringLiteral("expired")).toString(), Qt::ISODate);
    return expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc();
}

} // namespace

QList<CliProxyAccount> CliProxyCredentials::listAccounts(const QString &directory, const QString &type)
{
    QList<CliProxyAccount> accounts;
    const QStringList fileNames = QDir(directory).entryList({type + QStringLiteral("-*.json")}, QDir::Files, QDir::Name);
    for (const QString &fileName : fileNames) {
        const QJsonObject account = readAccountObject(directory, fileName);
        if (account.value(QStringLiteral("type")).toString() != type) {
            continue;
        }
        accounts.append({fileName, accountLabel(fileName, type), account.value(QStringLiteral("disabled")).toBool(),
                         accountExpired(account)});
    }
    return accounts;
}

CliProxyCredentialResult CliProxyCredentials::load(const QString &directory, const QString &type, const QString &fileName)
{
    QString resolvedFileName = fileName;
    if (resolvedFileName.isEmpty()) {
        const QList<CliProxyAccount> accounts = listAccounts(directory, type);
        if (accounts.isEmpty()) {
            return {false, {}, {}, QStringLiteral("No CLI Proxy API %1 accounts found in %2").arg(type, directory)};
        }
        if (accounts.size() > 1) {
            return {false, {}, {},
                    QStringLiteral("Multiple CLI Proxy API %1 accounts found; choose one in provider settings").arg(type)};
        }
        resolvedFileName = accounts.first().fileName;
    }

    const QJsonObject account = readAccountObject(directory, resolvedFileName);
    if (account.isEmpty()) {
        return {false, {}, {}, QStringLiteral("Could not read CLI Proxy API account %1 in %2").arg(resolvedFileName, directory)};
    }
    if (account.value(QStringLiteral("type")).toString() != type) {
        return {false, {}, {}, QStringLiteral("CLI Proxy API account %1 is not a %2 account").arg(resolvedFileName, type)};
    }
    if (account.value(QStringLiteral("disabled")).toBool()) {
        return {false, {}, {}, QStringLiteral("CLI Proxy API account %1 is disabled").arg(resolvedFileName)};
    }
    const QString accessToken = account.value(QStringLiteral("access_token")).toString().trimmed();
    if (accessToken.isEmpty()) {
        return {false, {}, {}, QStringLiteral("No access token in CLI Proxy API account %1").arg(resolvedFileName)};
    }
    // CLI Proxy API owns these tokens and refreshes them in place; Speecher only
    // reads, so an expired token means the proxy has not refreshed it yet.
    const QString expiredValue = account.value(QStringLiteral("expired")).toString();
    const QDateTime expiry = QDateTime::fromString(expiredValue, Qt::ISODate);
    if (!expiredValue.isEmpty() && !expiry.isValid()) {
        return {false, {}, {},
                QStringLiteral("Could not parse expiry \"%1\" in CLI Proxy API account %2").arg(expiredValue, resolvedFileName)};
    }
    if (expiry.isValid() && expiry <= QDateTime::currentDateTimeUtc()) {
        return {false, {}, {},
                QStringLiteral("CLI Proxy API token for %1 is expired; run CLI Proxy API to refresh it").arg(resolvedFileName)};
    }
    return {true, accessToken, account.value(QStringLiteral("account_id")).toString(), {}};
}

} // namespace speecher
