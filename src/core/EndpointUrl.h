#pragma once

#include <QString>

namespace speecher {

// The URL as typed, without surrounding spaces or trailing slashes.
inline QString withoutTrailingSlashes(const QString &url)
{
    QString base = url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

// The server's origin: trailing slashes and /v1 segments removed.
inline QString endpointServerBase(const QString &url)
{
    QString base = withoutTrailingSlashes(url);
    while (base.endsWith(QStringLiteral("/v1"))) {
        base = withoutTrailingSlashes(base.chopped(3));
    }
    return base;
}

inline QString endpointApiBase(const QString &url)
{
    const QString base = endpointServerBase(url);
    return base.isEmpty() ? QString() : base + QStringLiteral("/v1");
}

} // namespace speecher
