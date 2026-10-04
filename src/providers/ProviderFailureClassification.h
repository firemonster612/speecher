#pragma once

#include "core/ProviderFailure.h"

#include <QAbstractSocket>

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

// An error a service streamed after the request was accepted, by its code or
// type, such as Anthropic's "overloaded_error" or OpenAI's
// "rate_limit_exceeded". An unknown code is the server's failure; one naming
// an invalid or missing part of the request is Other.
ProviderFailureKind streamedErrorKind(const QString &code);

// A WebSocket error. Qt reports a refused upgrade as ConnectionRefusedError
// with the HTTP status only in the error string, so that status is read back
// from it.
ProviderFailureKind webSocketFailureKind(QAbstractSocket::SocketError error, const QString &errorString);

} // namespace speecher
