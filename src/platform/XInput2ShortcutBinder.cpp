#include "platform/XInput2ShortcutBinder.h"

#ifdef SPEECHER_WITH_X11

#include <QSocketNotifier>

// Xlib last: its macros (None, Bool, KeyPress) collide with Qt names.
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

namespace speecher {
namespace {

// X keycodes are one byte; the vocabulary's Fn key (evdev 464) lies past them.
constexpr int x11KeycodeOffset = 8;
constexpr int x11MaxKeycode = 255;

} // namespace

XInput2ShortcutBinder::XInput2ShortcutBinder(QObject *parent)
    : SingleKeyShortcutBinder(parent)
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        return;
    }
    int firstEvent = 0;
    int firstError = 0;
    int major = 2;
    int minor = 0;
    if (!XQueryExtension(m_display, "XInputExtension", &m_xiOpcode, &firstEvent, &firstError)
        || XIQueryVersion(m_display, &major, &minor) != Success) {
        XCloseDisplay(m_display);
        m_display = nullptr;
        return;
    }
    m_notifier = new QSocketNotifier(ConnectionNumber(m_display), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] { readEvents(); });
}

XInput2ShortcutBinder::~XInput2ShortcutBinder()
{
    if (m_display) {
        XCloseDisplay(m_display);
    }
}

bool XInput2ShortcutBinder::supported() const
{
    return m_display != nullptr;
}

QString XInput2ShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    const QString reason = SingleKeyShortcutBinder::unsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        return reason;
    }
    if (!m_display) {
        return QStringLiteral("Speecher could not reach the X server to watch a key.");
    }
    if (physicalKey(binding.keyCode())->evdev + x11KeycodeOffset > x11MaxKeycode) {
        return QStringLiteral("X11 does not report the %1 key.").arg(binding.displayText());
    }
    return QString();
}

QString XInput2ShortcutBinder::watch(const PhysicalKey &key)
{
    m_keycode = key.evdev + x11KeycodeOffset;
    resolveInjectionDevices();
    selectRawKeyEvents(true);
    return QString();
}

void XInput2ShortcutBinder::unwatch()
{
    if (m_keycode == 0) {
        return;
    }
    m_keycode = 0;
    selectRawKeyEvents(false);
}

// Text delivery on X11 injects keystrokes through ydotool, whose uinput
// keyboard shows up as an XInput2 slave device. Raw events name their source
// device, so dropping that device's events keeps a binding on a key delivery
// injects (Ctrl, Shift, V) from restarting dictation. Injected keys are still
// in flight after resume(), which is why the suspension window alone cannot
// close this loop. Hierarchy events re-resolve the id when ydotoold starts or
// stops.
void XInput2ShortcutBinder::resolveInjectionDevices()
{
    m_injectionDeviceIds.clear();
    int deviceCount = 0;
    XIDeviceInfo *devices = XIQueryDevice(m_display, XIAllDevices, &deviceCount);
    for (int index = 0; index < deviceCount; ++index) {
        if (QLatin1StringView(devices[index].name).startsWith(QLatin1StringView("ydotoold"))) {
            m_injectionDeviceIds.append(devices[index].deviceid);
        }
    }
    if (devices) {
        XIFreeDeviceInfo(devices);
    }
}

void XInput2ShortcutBinder::selectRawKeyEvents(bool select)
{
    unsigned char rawBits[XIMaskLen(XI_LASTEVENT)] = {};
    unsigned char hierarchyBits[XIMaskLen(XI_LASTEVENT)] = {};
    if (select) {
        XISetMask(rawBits, XI_RawKeyPress);
        XISetMask(rawBits, XI_RawKeyRelease);
        // Hierarchy events may only be selected for XIAllDevices, hence the
        // second mask.
        XISetMask(hierarchyBits, XI_HierarchyChanged);
    }
    XIEventMask masks[2];
    masks[0].deviceid = XIAllMasterDevices;
    masks[0].mask_len = sizeof(rawBits);
    masks[0].mask = rawBits;
    masks[1].deviceid = XIAllDevices;
    masks[1].mask_len = sizeof(hierarchyBits);
    masks[1].mask = hierarchyBits;
    XISelectEvents(m_display, DefaultRootWindow(m_display), masks, 2);
    XFlush(m_display);
}

// Text delivery may have injected the watched key (ydotool's Ctrl+V reaches
// the server as real input); the round trip makes sure those events are here
// before this pass runs. A press queued during suspended delivery must not
// start dictation, but a release must still clear the down-latch — the user
// letting go of the stop key during the synchronous delivery would otherwise
// leave m_down set and swallow the next press — and a hierarchy change must
// still re-resolve the injection devices.
void XInput2ShortcutBinder::resuming()
{
    if (!m_display) {
        return;
    }
    XSync(m_display, False);
    processQueuedEvents(false);
}

void XInput2ShortcutBinder::readEvents()
{
    processQueuedEvents(true);
}

void XInput2ShortcutBinder::processQueuedEvents(bool processPresses)
{
    while (XPending(m_display)) {
        XEvent event;
        XNextEvent(m_display, &event);
        XGenericEventCookie *cookie = &event.xcookie;
        if (cookie->type != GenericEvent || cookie->extension != m_xiOpcode
            || !XGetEventData(m_display, cookie)) {
            continue;
        }
        if (cookie->evtype == XI_HierarchyChanged) {
            resolveInjectionDevices();
            XFreeEventData(m_display, cookie);
            continue;
        }
        const auto *raw = static_cast<const XIRawEvent *>(cookie->data);
        if (raw->detail == m_keycode && !(raw->flags & XIKeyRepeat)
            && !m_injectionDeviceIds.contains(raw->sourceid)) {
            if (cookie->evtype == XI_RawKeyPress) {
                if (processPresses) {
                    keyDown();
                }
            } else if (cookie->evtype == XI_RawKeyRelease) {
                keyUp();
            }
        }
        XFreeEventData(m_display, cookie);
    }
}

} // namespace speecher

#endif // SPEECHER_WITH_X11
