#pragma once

#include <QWebSocketProtocol>

namespace speecher {

// Normal Closure, or a close frame without a status (1005), is the peer ending
// the stream on purpose. Claude Code draws the same line for its voice stream.
// A dropped connection reports errorOccurred before disconnected instead.
inline bool isCleanWebSocketClose(QWebSocketProtocol::CloseCode code)
{
    return code == QWebSocketProtocol::CloseCodeNormal
        || code == QWebSocketProtocol::CloseCodeMissingStatusCode;
}

} // namespace speecher
