#pragma once

#include <QAbstractSocket>

namespace speecher {

// Whether a socket error only reports that the server's side of a live stream
// went away. Qt cannot tell a server ending the stream from a dropped
// connection at this point: a clean close (the server sends a Close frame,
// then shuts TCP while the client's data is unread) resets the connection, and
// Qt reports RemoteHostClosedError, in ConnectedState, before the Close frame
// is visible through any public API; closeCode() reads 1000 either way. So a
// live stream the client has not asked to finish treats every remote end as
// the server ending it and rolls over; Qt always emits disconnected() after
// this error, and the disconnected handler does that. A stream that keeps
// ending straight away still fails: the Dictation Session treats an attempt
// that ends within its stable window as a failure.
inline bool isRemoteClose(QAbstractSocket::SocketError error)
{
    return error == QAbstractSocket::RemoteHostClosedError;
}

} // namespace speecher
