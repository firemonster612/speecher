#include "app/ProvidersCommand.h"

#include "app/LocalSetup.h"
#include "app/NetworkReachability.h"
#include "app/ProviderAvailability.h"
#include "app/ProviderSetup.h"
#include "app/SingleInstanceIpc.h"
#include "core/SettingsStore.h"
#include "providers/CliProxyCredentials.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"
#include "providers/ProviderSignIn.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <ostream>

namespace speecher {
namespace {

const QString kProvidersCommand = QStringLiteral("providers");

QString roleName(ProviderRole role)
{
    return role == ProviderRole::Speech ? QStringLiteral("speech") : QStringLiteral("refinement");
}

QJsonValue jsonValue(std::optional<bool> value)
{
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

std::optional<bool> optionalBool(const QJsonValue &value)
{
    return value.isBool() ? std::optional(value.toBool()) : std::nullopt;
}

std::optional<QList<ProviderReport>> reportsFromJson(const QString &text)
{
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    if (!document.isArray()) {
        return std::nullopt;
    }
    QList<ProviderReport> reports;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const bool speech = object.value(QStringLiteral("role")).toString() == roleName(ProviderRole::Speech);
        reports.append({
            object.value(QStringLiteral("id")).toString(),
            speech ? ProviderRole::Speech : ProviderRole::Refinement,
            object.value(QStringLiteral("label")).toString(),
            object.value(QStringLiteral("signsIn")).toBool(),
            object.value(QStringLiteral("configured")).toBool(),
            optionalBool(object.value(QStringLiteral("signedIn"))),
            optionalBool(object.value(QStringLiteral("usable"))),
            object.value(QStringLiteral("problem")).toString(),
        });
    }
    return reports;
}

// Columns two spaces apart, each as wide as its widest cell.
void printTable(const QList<ProviderReport> &reports, std::ostream &out)
{
    const QList<QStringList> rows = providerReportTable(reports);
    QList<qsizetype> widths(rows.first().size(), 0);
    for (const QStringList &row : rows) {
        for (qsizetype column = 0; column < row.size(); ++column) {
            widths[column] = std::max(widths[column], row[column].size());
        }
    }
    for (const QStringList &row : rows) {
        QString line;
        for (qsizetype column = 0; column < row.size(); ++column) {
            line += row[column].leftJustified(widths[column] + 2);
        }
        out << line.trimmed().toStdString() << '\n';
    }
}

// Notes the sign-ins the CLI Proxy API account files settle without a
// keyring, a refresh or a network call: one whose account isn't there is
// signed out. Every other sign-in is read from a keyring or refreshed on use.
void noteMissingCliproxyAccounts(SettingsStore &settings, ProviderAvailability &availability)
{
    // A remote CLI Proxy API picks its own accounts.
    if (!settings.cliproxyBaseUrl().isEmpty()) {
        return;
    }
    const ProviderSignIn signIn(settings);
    for (const QString &providerId : {QStringLiteral("claude"), QStringLiteral("codex"), QStringLiteral("openai"),
                                      QStringLiteral("anthropic")}) {
        // Also the speech provider whose sign-in a refinement provider shares.
        const QString type = ProviderSignIn::cliproxyAccountType(providerId);
        if (!signIn.usingCliproxy(type)) {
            continue;
        }
        const QString chosen = signIn.cliproxyAccount(type);
        const QList<CliProxyAccount> accounts =
            CliProxyCredentials::listAccounts(signIn.resolvedAccountDirectory(), type);
        const bool found = chosen.isEmpty() ? !accounts.isEmpty()
                                            : std::any_of(accounts.cbegin(), accounts.cend(),
                                                          [&](const CliProxyAccount &account) {
                                                              return account.fileName == chosen;
                                                          });
        if (!found) {
            availability.noteSignIn(providerId, false);
        }
    }
}

QList<ProviderReport> localProviderReports()
{
    SettingsStore settings;
    LocalModelStore localModels;
    ProviderRegistry providers;
    registerProviders(providers, settings.secrets(), &localModels);
    LocalSetup local(settings, providers, localModels);
    NetworkReachability reachability;
    reachability.watchSystem();
    ProviderAvailability availability(reachability);
    noteMissingCliproxyAccounts(settings, availability);
    local.setProviderAvailability(availability);
    return providerReports(settings, local, providers);
}

} // namespace

QList<ProviderReport> providerReports(const SettingsStore &settings, const LocalSetup &local,
                                      const ProviderRegistry &providers)
{
    return providerReports(settings.dictationSnapshot(), local.liveFacts(),
                           providers.rowOptions(ProviderRole::Speech), providers.rowOptions(ProviderRole::Refinement));
}

QByteArray providerReportsJson(const QList<ProviderReport> &reports)
{
    QJsonArray array;
    for (const ProviderReport &report : reports) {
        array.append(QJsonObject{
            {QStringLiteral("id"), report.id},
            {QStringLiteral("role"), roleName(report.role)},
            {QStringLiteral("label"), report.label},
            {QStringLiteral("signsIn"), report.signsIn},
            {QStringLiteral("configured"), report.configured},
            {QStringLiteral("signedIn"), jsonValue(report.signedIn)},
            {QStringLiteral("usable"), jsonValue(report.usable)},
            {QStringLiteral("problem"),
             report.problem.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(report.problem)},
        });
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

void printProviderReports(const QList<ProviderReport> &reports, bool json, std::ostream &out)
{
    if (json) {
        out << providerReportsJson(reports).toStdString() << '\n';
    } else {
        printTable(reports, out);
    }
}

std::optional<QList<ProviderReport>> runningAppProviderReports(
    const std::shared_ptr<const SingleInstancePlatform> &platform, QString *error)
{
    IpcResponse response;
    const IpcCommandResult result =
        SingleInstanceIpc::sendCommandDetailed(kProvidersCommand, &response, 2500, platform, error);
    // sendCommandDetailed() says why an app that runs didn't answer.
    if (result != IpcCommandResult::Sent || response.message == kUnknownIpcCommandMessage) {
        return std::nullopt;
    }
    std::optional<QList<ProviderReport>> reports = reportsFromJson(response.text);
    if (!reports) {
        *error = QStringLiteral("The running Speecher answered `providers` with something other than a list");
    }
    return reports;
}

int runProvidersCommand(bool json, const std::shared_ptr<const SingleInstancePlatform> &platform,
                        std::ostream &out, std::ostream &err)
{
    QString error;
    std::optional<QList<ProviderReport>> reports = runningAppProviderReports(platform, &error);
    if (!error.isEmpty()) {
        err << error.toStdString() << '\n';
        return 1;
    }
    printProviderReports(reports ? *reports : localProviderReports(), json, out);
    return 0;
}

} // namespace speecher
