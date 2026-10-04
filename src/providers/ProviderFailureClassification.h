#pragma once

#include "core/ProviderFailure.h"

#include <QAbstractSocket>

class QJsonObject;
class QNetworkReply;

namespace speecher {

// 401 and 403 are a rejected sign-in, 429 a rate limit, 5xx the server's own
// failure; any other status is Other.
ProviderFailureKind failureKindForHttpStatus(int httpStatus);

// A finished reply that failed: classified by its HTTP status when the server
// refused the request, otherwise by the transport error, so a stream cut off
// after a 200 is a network failure. A reply this client aborted is Other;
// whoever aborted it knows why.
ProviderFailure replyFailure(const QNetworkReply &reply, const QString &message);

// An error object a service streamed after the request was accepted. Its
// specific code, a string or a number, decides first, so
// {"type":"invalid_request_error","code":"invalid_api_key"} is a rejected
// sign-in; the generic type decides only when the code names no kind. Codes
// such as Anthropic's "overloaded_error" or OpenAI's "rate_limit_exceeded"
// are known; an unknown one is the server's failure, and one naming an
// invalid or missing part of the request is Other.
ProviderFailureKind streamedErrorKind(const QJsonObject &error);

// A failure from a Local Runner or a Custom Endpoint. A 404 there means the
// model or the path is gone, so the provider is Unavailable; a cloud
// provider's 404 stays Other.
ProviderFailure selfHostedFailure(ProviderFailure failure);

// A WebSocket error. Qt reports a refused upgrade as ConnectionRefusedError
// with the HTTP status only in the error string, so that status is read back
// from it.
ProviderFailureKind webSocketFailureKind(QAbstractSocket::SocketError error, const QString &errorString);

} // namespace speecher
