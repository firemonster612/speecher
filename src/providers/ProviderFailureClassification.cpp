#include "providers/ProviderFailureClassification.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>

#include <optional>

namespace speecher {

ProviderFailureKind failureKindForHttpStatus(int httpStatus)
{
    if (httpStatus == 401 || httpStatus == 403) return ProviderFailureKind::Authentication;
    if (httpStatus == 429) return ProviderFailureKind::RateLimited;
    if (httpStatus >= 500 && httpStatus <= 599) return ProviderFailureKind::Server;
    return ProviderFailureKind::Other;
}

ProviderFailure replyFailure(const QNetworkReply &reply, const QString &message)
{
    const int httpStatus = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus >= 400) return {failureKindForHttpStatus(httpStatus), message, httpStatus};
    const QNetworkReply::NetworkError error = reply.error();
    if (error == QNetworkReply::TimeoutError) return {ProviderFailureKind::Timeout, message, httpStatus};
    // Codes below 200 are the connection's and the proxy's: no answer came,
    // or it was cut off.
    const bool connectionFailed = error != QNetworkReply::OperationCanceledError
        && error > QNetworkReply::NoError && error < QNetworkReply::ContentAccessDenied;
    return {connectionFailed ? ProviderFailureKind::Network : ProviderFailureKind::Other, message, httpStatus};
}

namespace {

// The kind an error code or type names, if it names one.
std::optional<ProviderFailureKind> knownStreamedErrorKind(const QString &code)
{
    static const QStringList authentication{
        QStringLiteral("authentication_error"), QStringLiteral("permission_error"),
        QStringLiteral("invalid_api_key"), QStringLiteral("401"), QStringLiteral("403")};
    static const QStringList rateLimited{
        QStringLiteral("rate_limit_error"), QStringLiteral("rate_limit_exceeded"),
        QStringLiteral("insufficient_quota"), QStringLiteral("429")};
    if (authentication.contains(code)) return ProviderFailureKind::Authentication;
    if (rateLimited.contains(code)) return ProviderFailureKind::RateLimited;
    if (code.contains(QStringLiteral("invalid")) || code.contains(QStringLiteral("not_found"))) {
        return ProviderFailureKind::Other;
    }
    return std::nullopt;
}

} // namespace

ProviderFailureKind streamedErrorKind(const QJsonObject &error)
{
    const QString code = error.value(QStringLiteral("code")).toVariant().toString();
    const QString type = error.value(QStringLiteral("type")).toString();
    return knownStreamedErrorKind(code)
        .value_or(knownStreamedErrorKind(type).value_or(ProviderFailureKind::Server));
}

ProviderFailure selfHostedFailure(ProviderFailure failure)
{
    if (failure.httpStatus == 404) failure.kind = ProviderFailureKind::Unavailable;
    return failure;
}

ProviderFailureKind webSocketFailureKind(QAbstractSocket::SocketError error, const QString &errorString)
{
    static const QRegularExpression handshakeStatus(QStringLiteral("status code: (\\d{3})"));
    if (const int httpStatus = handshakeStatus.match(errorString).captured(1).toInt()) {
        return failureKindForHttpStatus(httpStatus);
    }
    switch (error) {
    case QAbstractSocket::SocketTimeoutError:
        return ProviderFailureKind::Timeout;
    case QAbstractSocket::ConnectionRefusedError:
    case QAbstractSocket::RemoteHostClosedError:
    case QAbstractSocket::HostNotFoundError:
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::SslHandshakeFailedError:
    case QAbstractSocket::ProxyConnectionRefusedError:
    case QAbstractSocket::ProxyConnectionClosedError:
    case QAbstractSocket::ProxyConnectionTimeoutError:
    case QAbstractSocket::ProxyNotFoundError:
    case QAbstractSocket::TemporaryError:
        return ProviderFailureKind::Network;
    default:
        return ProviderFailureKind::Other;
    }
}

} // namespace speecher
