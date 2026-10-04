// Plasma check for the session-only Cancel Shortcut: bind C through
// KGlobalAccelShortcutBinder, then type C into KWin through its EIS
// (libei) RemoteDesktop endpoint, which is real compositor input, and
// count activations while disarmed, armed, and disarmed again. Also
// times setArmed(), since it runs on every session start and end.

#include "core/ShortcutBinding.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/KGlobalAccelShortcutBinder.h"

#include <QApplication>
#include <QKeyEvent>
#include <QWidget>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTest>

#include <libei.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstring>

using namespace speecher;

namespace {

ei *g_ei = nullptr;
ei_device *g_keyboard = nullptr;
uint32_t g_sequence = 1;

void dispatch()
{
    ei_dispatch(g_ei);
    while (ei_event *event = ei_get_event(g_ei)) {
        switch (ei_event_get_type(event)) {
        case EI_EVENT_SEAT_ADDED:
            ei_seat_bind_capabilities(ei_event_get_seat(event), EI_DEVICE_CAP_KEYBOARD, nullptr);
            break;
        case EI_EVENT_DEVICE_RESUMED:
            if (ei_device_has_capability(ei_event_get_device(event), EI_DEVICE_CAP_KEYBOARD)) {
                g_keyboard = ei_device_ref(ei_event_get_device(event));
            }
            break;
        case EI_EVENT_DISCONNECT:
            std::fprintf(stderr, "EIS disconnected\n");
            break;
        default:
            break;
        }
        ei_event_unref(event);
    }
}

bool connectToKWin()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/EIS/RemoteDesktop"),
        QStringLiteral("org.kde.KWin.EIS.RemoteDesktop"), QStringLiteral("connectToEIS"));
    // The portal's device types: keyboard 1, pointer 2, touchscreen 4.
    call << 7;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        std::fprintf(stderr, "connectToEIS failed: %s\n", qPrintable(reply.errorMessage()));
        return false;
    }
    const auto fd = qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments().first());
    g_ei = ei_new_sender(nullptr);
    ei_configure_name(g_ei, "speecher-pause-harness");
    if (ei_setup_backend_fd(g_ei, dup(fd.fileDescriptor())) != 0) {
        std::fprintf(stderr, "ei_setup_backend_fd failed\n");
        return false;
    }
    auto *notifier = new QSocketNotifier(ei_get_fd(g_ei), QSocketNotifier::Read, qApp);
    QObject::connect(notifier, &QSocketNotifier::activated, [] { dispatch(); });
    if (!QTest::qWaitFor([] { dispatch(); return g_keyboard != nullptr; }, 5000)) {
        std::fprintf(stderr, "KWin offered no keyboard over EIS\n");
        return false;
    }
    return true;
}

void typeC()
{
    ei_device_start_emulating(g_keyboard, g_sequence++);
    ei_device_keyboard_key(g_keyboard, KEY_C, true);
    ei_device_frame(g_keyboard, ei_now(g_ei));
    QTest::qWait(80);
    ei_device_keyboard_key(g_keyboard, KEY_C, false);
    ei_device_frame(g_keyboard, ei_now(g_ei));
    ei_device_stop_emulating(g_keyboard);
    QTest::qWait(400);
}

// The focused app: counts the C presses that reach it.
class FocusedApp : public QWidget {
public:
    int presses = 0;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        presses += event->key() == Qt::Key_C && !event->isAutoRepeat();
    }
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    GlobalShortcutAction action = GlobalShortcutBinder::actionFor(GlobalShortcutRole::Cancel);
    action.settingsKey = QStringLiteral("pauseHarness/cancel");
    KGlobalAccelSessionShortcutBinder binder(action);
    if (!binder.supported()) {
        std::fprintf(stderr, "KGlobalAccel not supported (XDG_CURRENT_DESKTOP?)\n");
        return 1;
    }
    if (argc > 1 && std::strcmp(argv[1], "after-crash") == 0) {
        // A new run after a crash: does C reach the focused app before
        // Speecher's own startup cleanup, and after it?
        if (!connectToKWin()) {
            return 1;
        }
        FocusedApp focused;
        focused.resize(200, 200);
        focused.show();
        focused.activateWindow();
        QTest::qWaitForWindowActive(&focused, 5000);
        QTest::qWait(500);
        typeC();
        const int before = focused.presses;
        binder.bind();
        QTest::qWait(300);
        typeC();
        std::printf("after a crash while armed, C reaches the focused app: before startup cleanup %d, "
                    "after bind() %d\n", before, focused.presses - before);
        binder.setShortcut({});
        return 0;
    }
    binder.bind();
    QString error;
    if (!binder.setShortcut(ShortcutBinding(QKeySequence(Qt::Key_C)), &error)) {
        std::fprintf(stderr, "setShortcut(C) failed: %s\n", qPrintable(error));
        return 1;
    }
    if (!connectToKWin()) {
        return 1;
    }
    const bool crashMode = argc > 1 && std::strcmp(argv[1], "crash") == 0;
    if (crashMode) {
        binder.setArmed(true);
        QTest::qWait(300);
        std::puts("armed C, now dying with SIGKILL");
        std::fflush(stdout);
        std::raise(SIGKILL);
    }
    QSignalSpy activated(&binder, &GlobalShortcutBinder::activated);
    FocusedApp focused;
    focused.resize(200, 200);
    focused.show();
    focused.activateWindow();
    QTest::qWaitForWindowActive(&focused, 5000);
    QTest::qWait(500);

    typeC();
    const qsizetype disarmed = activated.count();
    const int appDisarmed = focused.presses;

    QElapsedTimer timer;
    timer.start();
    binder.setArmed(true);
    const qint64 armMs = timer.elapsed();
    QTest::qWait(300);
    typeC();
    const qsizetype armed = activated.count() - disarmed;
    const int appArmed = focused.presses - appDisarmed;

    timer.restart();
    binder.setArmed(false);
    const qint64 disarmMs = timer.elapsed();
    QTest::qWait(300);
    typeC();
    const qsizetype after = activated.count() - disarmed - armed;
    const int appAfter = focused.presses - appDisarmed - appArmed;

    binder.setShortcut({});
    std::printf("C presses acting as Cancel: disarmed %lld, armed %lld, disarmed again %lld\n",
                static_cast<long long>(disarmed), static_cast<long long>(armed),
                static_cast<long long>(after));
    std::printf("setArmed(true) took %lld ms, setArmed(false) took %lld ms\n",
                static_cast<long long>(armMs), static_cast<long long>(disarmMs));
    std::printf("C presses reaching the focused app: disarmed %d, armed %d, disarmed again %d "
                "(window active: %d)\n",
                appDisarmed, appArmed, appAfter, int(focused.isActiveWindow()));
    return disarmed == 0 && armed == 1 && after == 0 && appDisarmed == 1 && appArmed == 0
            && appAfter == 1
        ? 0
        : 1;
}
