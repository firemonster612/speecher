#pragma once

#include <QWebSocketProtocol>

namespace speecher {

// Call only from a disconnected handler that has already ruled out a reported
// error and a close this client started itself. The close code cannot tell a
// drop from a clean close: Qt 6.8 starts it at CloseCodeNormal and changes it
// only inside close(), so an abort, FIN or reset still reads 1000, and an empty
// close frame also reads 1000. What separates them is ordering: a dropped
// connection emits errorOccurred before disconnected, so the client's failure
// flag is already set when this runs. Anything that closes or aborts the socket
// must set that flag, or a cancelled/completed flag, first.
inline bool isCleanWebSocketClose(QWebSocketProtocol::CloseCode code)
{
    return code == QWebSocketProtocol::CloseCodeNormal;
}

} // namespace speecher
