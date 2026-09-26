#pragma once

#include <QString>

namespace speecher {

inline QString endpointServerBase(const QString &url)
{
    QString base = url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    while (base.endsWith(QStringLiteral("/v1"))) {
        base.chop(3);
        while (base.endsWith(QLatin1Char('/'))) {
            base.chop(1);
        }
    }
    return base;
}

inline QString endpointApiBase(const QString &url)
{
    const QString base = endpointServerBase(url);
    return base.isEmpty() ? QString() : base + QStringLiteral("/v1");
}

} // namespace speecher
