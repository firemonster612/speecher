// Proves the X11 shortcut backends end to end through XTEST: the XInput2
// single-key backend reports a right Alt press and release, and the key grab
// that serves the Cancel and Pause Shortcuts takes a bare C only while armed,
// acting once per press however long the key is held.
// Runs under Xvfb in CI, never against a real session: XTEST here would type
// into whatever the user has focused. CMake only registers this test where
// Xvfb is present, and it is launched through xvfb-run on a private display.

#include "core/ShortcutBinding.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/X11KeyGrabShortcutBinder.h"
#include "platform/XInput2ShortcutBinder.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <cstdio>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

using namespace speecher;

namespace {

// Right Alt is X11 keycode 108 (evdev 100 + 8), which is exactly the key the
// binder computes from the "AltRight" vocabulary row.
constexpr int rightAltKeycode = 108;

} // namespace

// Presses C through XTEST, holding it long enough to auto-repeat.
void pressC(Display *display)
{
    const int keycode = XKeysymToKeycode(display, XK_c);
    XTestFakeKeyEvent(display, keycode, True, 0);
    XFlush(display);
    QTest::qWait(700);
    XTestFakeKeyEvent(display, keycode, False, 0);
    XFlush(display);
    QTest::qWait(200);
}

int checkKeyGrab()
{
    GlobalShortcutAction action = GlobalShortcutBinder::actionFor(GlobalShortcutRole::Cancel);
    action.settingsKey = QStringLiteral("x11InjectionTest/cancel");
    X11KeyGrabShortcutBinder binder(action);
    QString error;
    if (!binder.setShortcut(ShortcutBinding(QKeySequence(Qt::Key_C)), &error)) {
        std::fprintf(stderr, "could not bind C: %s\n", qPrintable(error));
        return 1;
    }
    QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);
    Display *display = XOpenDisplay(nullptr);
    XAutoRepeatOn(display);
    // A focused window stands in for the app the person types into.
    const Window app = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 100, 100,
                                           0, 0, 0);
    XSelectInput(display, app, KeyPressMask);
    XMapWindow(display, app);
    XSync(display, False);
    XSetInputFocus(display, app, RevertToParent, CurrentTime);
    XSync(display, False);
    const auto pressesSeenByApp = [display] {
        int presses = 0;
        XSync(display, False);
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            presses += event.type == KeyPress;
        }
        return presses;
    };

    pressC(display);
    const int appBeforeArming = pressesSeenByApp();
    binder.setArmed(true);
    pressC(display);
    const int appWhileArmed = pressesSeenByApp();
    binder.setArmed(false);
    pressC(display);
    const int appAfterDisarming = pressesSeenByApp();
    XCloseDisplay(display);
    binder.setShortcut({});
    if (activated.count() != 1 || appBeforeArming == 0 || appWhileArmed != 0
        || appAfterDisarming == 0) {
        std::fprintf(stderr,
                     "expected one activation, and C reaching the app only while disarmed; got "
                     "%lld activations, app presses %d / %d / %d\n",
                     static_cast<long long>(activated.count()), appBeforeArming, appWhileArmed,
                     appAfterDisarming);
        return 1;
    }
    std::puts("X11 key grab: a held C acted once while armed, the focused window got C only "
              "while disarmed");
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int keyGrab = checkKeyGrab();

    XInput2ShortcutBinder binder;
    if (!binder.supported()) {
        std::fprintf(stderr, "XInput2 binder could not reach the X server\n");
        return 1;
    }

    QString error;
    if (!binder.setShortcut(ShortcutBinding::singleKey(QStringLiteral("AltRight")), &error)) {
        std::fprintf(stderr, "could not watch AltRight: %s\n", qPrintable(error));
        return 1;
    }

    QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);
    QSignalSpy deactivated(&binder, &GlobalShortcutBinder::deactivated);

    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        std::fprintf(stderr, "could not open a second connection for injection\n");
        return 1;
    }

    QTimer::singleShot(200, [display] {
        XTestFakeKeyEvent(display, rightAltKeycode, True, 0);
        XFlush(display);
    });
    QTimer::singleShot(400, [display] {
        XTestFakeKeyEvent(display, rightAltKeycode, False, 0);
        XFlush(display);
    });

    if (activated.count() == 0) {
        activated.wait(3000);
    }
    if (deactivated.count() == 0) {
        deactivated.wait(3000);
    }
    XCloseDisplay(display);

    if (activated.count() != 1 || deactivated.count() != 1) {
        std::fprintf(stderr, "expected one activated and one deactivated; got %lld and %lld\n",
                     static_cast<long long>(activated.count()),
                     static_cast<long long>(deactivated.count()));
        return 1;
    }
    std::puts("XInput2 backend: activated() then deactivated() fired on injected right Alt");
    return keyGrab;
}
