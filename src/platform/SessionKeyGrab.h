#pragma once

#include <QObject>

namespace speecher {

// Takes Escape or Enter system-wide while a Dictation Session needs it, so a
// press acts on the session and the focused app never sees the key. Windows
// and macOS take Escape through a whole session; every platform that can
// take a key takes both while a selection edit is reviewed. A Wayland client
// outside Plasma can only observe a key, so the key would also reach the app
// (and interrupt a coding agent in a terminal); there it takes none.
class SessionKeyGrab : public QObject {
    Q_OBJECT

public:
    enum class Key {
        Escape,
        Enter,
    };
    Q_ENUM(Key)

    using QObject::QObject;

    // Takes the key, or gives it back. Returns whether it is held afterwards:
    // false when another app holds it.
    virtual bool setGrabbed(Key key, bool grabbed) = 0;

signals:
    void pressed(speecher::SessionKeyGrab::Key key);
};

} // namespace speecher
