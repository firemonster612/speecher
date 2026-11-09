#pragma once

// A Service Order as the speechServices and refinementServices Settings rows
// take it, for the UI tests.

#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace speecher::test {

// One entry per service, naming only it, so each keeps its saved choice.
inline QVariantList serviceOrder(const QStringList &services)
{
    QVariantList entries;
    for (const QString &service : services) {
        entries.append(QVariantMap{{QStringLiteral("service"), service}});
    }
    return entries;
}

// The services of an order as the rows hand it back.
inline QStringList servicesOf(const QVariant &order)
{
    QStringList services;
    for (const QVariant &entry : order.toList()) {
        services.append(entry.toMap().value(QStringLiteral("service")).toString());
    }
    return services;
}

} // namespace speecher::test
