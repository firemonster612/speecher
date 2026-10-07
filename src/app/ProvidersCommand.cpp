#include "app/ProvidersCommand.h"

#include "app/LocalSetup.h"
#include "app/NetworkReachability.h"
#include "app/ProviderAvailability.h"
#include "app/ProviderSetup.h"
#include "core/SettingsStore.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <ostream>

namespace speecher {
namespace {

QList<RowOption> providerOptions(const QList<ProviderDescriptor> &providers)
{
    QList<RowOption> options;
    for (const ProviderDescriptor &provider : providers) {
        options.append({provider.id, provider.label});
    }
    return options;
}

QString roleName(ProviderRole role)
{
    return role == ProviderRole::Speech ? QStringLiteral("speech") : QStringLiteral("refinement");
}

QJsonValue jsonValue(std::optional<bool> value)
{
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QString tableValue(std::optional<bool> value)
{
    if (!value) {
        return QStringLiteral("unknown");
    }
    return *value ? QStringLiteral("yes") : QStringLiteral("no");
}

void printJson(const QList<ProviderReport> &reports, std::ostream &out)
{
    QJsonArray array;
    for (const ProviderReport &report : reports) {
        array.append(QJsonObject{
            {QStringLiteral("id"), report.id},
            {QStringLiteral("role"), roleName(report.role)},
            {QStringLiteral("label"), report.label},
            {QStringLiteral("configured"), report.configured},
            {QStringLiteral("signedIn"), jsonValue(report.signedIn)},
            {QStringLiteral("usable"), jsonValue(report.usable)},
            {QStringLiteral("problem"),
             report.problem.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(report.problem)},
        });
    }
    out << QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString() << '\n';
}

// Columns two spaces apart, each as wide as its widest cell.
void printTable(const QList<ProviderReport> &reports, std::ostream &out)
{
    QList<QStringList> rows{{QStringLiteral("ROLE"), QStringLiteral("ID"), QStringLiteral("NAME"),
                             QStringLiteral("CONFIGURED"), QStringLiteral("SIGNED IN"), QStringLiteral("USABLE"),
                             QStringLiteral("PROBLEM")}};
    for (const ProviderReport &report : reports) {
        rows.append({roleName(report.role), report.id, report.label,
                     report.configured ? QStringLiteral("yes") : QStringLiteral("no"),
                     // Unknown only for a provider that signs in.
                     providerSignsIn(report.id) ? tableValue(report.signedIn) : QStringLiteral("-"),
                     tableValue(report.usable), report.problem});
    }
    QList<qsizetype> widths(rows.first().size(), 0);
    for (const QStringList &row : std::as_const(rows)) {
        for (qsizetype column = 0; column < row.size(); ++column) {
            widths[column] = std::max(widths[column], row[column].size());
        }
    }
    for (const QStringList &row : std::as_const(rows)) {
        QString line;
        for (qsizetype column = 0; column < row.size(); ++column) {
            line += row[column].leftJustified(widths[column] + 2);
        }
        out << line.trimmed().toStdString() << '\n';
    }
}

} // namespace

void printProviderReports(const QList<ProviderReport> &reports, bool json, std::ostream &out)
{
    if (json) {
        printJson(reports, out);
    } else {
        printTable(reports, out);
    }
}

int runProvidersCommand(bool json, std::ostream &out)
{
    SettingsStore settings;
    LocalModelStore localModels;
    ProviderRegistry providers;
    registerProviders(providers, settings.secrets(), &localModels);
    LocalSetup local(settings, providers, localModels);
    NetworkReachability reachability;
    reachability.watchSystem();
    ProviderAvailability availability(reachability);
    local.setProviderAvailability(availability);
    printProviderReports(providerReports(settings.dictationSnapshot(), local.liveFacts(),
                                         providerOptions(providers.speechProviders()),
                                         providerOptions(providers.refinementProviders())),
                         json, out);
    return 0;
}

} // namespace speecher
