#pragma once

#include <QKeySequence>
#include <QObject>

namespace speecher {

// Takes a key combination from the desktop on demand, so its presses reach
// Speecher instead of the focused app while it is held: a bare key such as C
// or Escape types as usual the rest of the time.
class KeyHold : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Takes the keys from the desktop; false when another app holds them.
    // Holds one combination at a time: let go before taking another.
    virtual bool take(const QKeySequence &keys) = 0;
    // Lets go of whatever take() holds; a no-op when it holds nothing.
    virtual void letGo() = 0;

signals:
    void pressed();
};

} // namespace speecher
