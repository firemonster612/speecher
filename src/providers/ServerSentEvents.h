#pragma once

#include <QByteArray>

#include <optional>

namespace speecher {

struct SseFrame {
    QByteArray name;
    QByteArray data;
};

// Removes the first complete event from buffer, or returns nothing while the
// buffer holds only part of one.
inline std::optional<SseFrame> takeSseFrame(QByteArray &buffer)
{
    int boundary = buffer.indexOf("\n\n");
    int separatorBytes = 2;
    const int crlfBoundary = buffer.indexOf("\r\n\r\n");
    if (crlfBoundary >= 0 && (boundary < 0 || crlfBoundary < boundary)) {
        boundary = crlfBoundary;
        separatorBytes = 4;
    }
    if (boundary < 0) return std::nullopt;
    const QByteArray frame = buffer.left(boundary);
    buffer.remove(0, boundary + separatorBytes);
    SseFrame event;
    for (const QByteArray &line : frame.split('\n')) {
        if (line.startsWith("event:")) {
            event.name = line.mid(6).trimmed();
        } else if (line.startsWith("data:")) {
            event.data += line.mid(5).trimmed();
        }
    }
    return event;
}

} // namespace speecher
