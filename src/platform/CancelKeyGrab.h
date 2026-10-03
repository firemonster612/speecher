#pragma once

#include <QObject>

namespace speecher {

// Takes Escape system-wide while a Dictation Session can be cancelled, so a
// press cancels it and the focused app never sees the key. Windows and macOS
// only: a Wayland client can only observe a key, so Escape would also reach
// the app (and interrupt a coding agent in a terminal).
class CancelKeyGrab : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Takes Escape, or gives it back.
    virtual void setGrabbed(bool grabbed) = 0;

signals:
    void pressed();
};

} // namespace speecher
