#pragma once

#include <QString>
#include <QUrl>

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

// Whether two endpoint URLs name the same server: one scheme, host and port,
// whatever their paths. A URL without a host names no server.
inline bool sameOrigin(const QString &first, const QString &second)
{
    const QUrl a(first.trimmed());
    const QUrl b(second.trimmed());
    const auto port = [](const QUrl &url) { return url.port(url.scheme() == QStringLiteral("https") ? 443 : 80); };
    return !a.host().isEmpty() && a.scheme() == b.scheme() && a.host() == b.host() && port(a) == port(b);
}

inline QString endpointApiBase(const QString &url)
{
    const QString base = endpointServerBase(url);
    return base.isEmpty() ? QString() : base + QStringLiteral("/v1");
}

} // namespace speecher
