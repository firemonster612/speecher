#include "platform/mac/MacSingleKeyShortcutBinder.h"

#include <QTimer>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <IOKit/hidsystem/IOLLEvent.h>

#include <unistd.h>

#include <optional>

namespace speecher {
namespace {

// System Settings has no way to tell a running process about a new grant, so
// noticing it means asking again, the same cadence the app's own
// accessibility poll uses.
constexpr int grantPollMs = 1000;

QString accessibilityRequiredReason()
{
    return QStringLiteral(
        "Grant Speecher Accessibility access in System Settings before it can watch a single key");
}

// The flag bit that says whether this modifier key is down. Bare modifiers
// only report through flagsChanged, and the left twin must not read as the
// right one: IOLLEvent.h's device-dependent NX_DEVICE* bits tell them apart,
// where the portable NSEventModifierFlagOption would blur both Options into
// one. Deriving down-vs-up from the bit, not from alternating edges, keeps a
// stale edge while the twin is held from desyncing the state.
//
// Caps Lock has no release event: its bit is the LOCK state, which flips once
// per press and never on release, so it cannot be mapped to down/up like the
// other modifiers; the observe block special-cases it as one press per event.
std::optional<NSUInteger> modifierFlagBit(int macKeyCode)
{
    switch (macKeyCode) {
    case kVK_Shift:
        return NX_DEVICELSHIFTKEYMASK;
    case kVK_RightShift:
        return NX_DEVICERSHIFTKEYMASK;
    case kVK_Control:
        return NX_DEVICELCTLKEYMASK;
    case kVK_RightControl:
        return NX_DEVICERCTLKEYMASK;
    case kVK_Option:
        return NX_DEVICELALTKEYMASK;
    case kVK_RightOption:
        return NX_DEVICERALTKEYMASK;
    case kVK_Command:
        return NX_DEVICELCMDKEYMASK;
    case kVK_RightCommand:
        return NX_DEVICERCMDKEYMASK;
    case kVK_CapsLock:
        return NSEventModifierFlagCapsLock;
    case kVK_Function:
        return NSEventModifierFlagFunction;
    default:
        return std::nullopt;
    }
}

// Text delivery pastes with a synthetic Cmd+V, which the monitors would see:
// a bound V (or any key delivery ever posts) must not restart dictation from
// Speecher's own paste. Suspending around the post cannot cover it — the
// monitor callback arrives on a later runloop turn, after any resume — so the
// binder drops events this process posted instead. The e2e harness posts from
// another process, whose events keep their own PID and still arrive.
bool postedBySpeecher(NSEvent *event)
{
    CGEventRef cgEvent = event.CGEvent;
    return cgEvent
        && CGEventGetIntegerValueField(cgEvent, kCGEventSourceUnixProcessID) == getpid();
}

} // namespace

MacSingleKeyShortcutBinder::MacSingleKeyShortcutBinder(QObject *parent)
    : SingleKeyShortcutBinder(parent)
{
    if (AXIsProcessTrusted()) {
        return;
    }
    m_grantPoll = new QTimer(this);
    m_grantPoll->setInterval(grantPollMs);
    connect(m_grantPoll, &QTimer::timeout, this, [this] {
        if (!AXIsProcessTrusted()) {
            return;
        }
        m_grantPoll->stop();
        // Re-reads the stored binding; a no-op when none is a single key.
        bind();
        emit supportChanged();
    });
    m_grantPoll->start();
}

MacSingleKeyShortcutBinder::~MacSingleKeyShortcutBinder()
{
    unwatch();
}

bool MacSingleKeyShortcutBinder::supported() const
{
    return true;
}

QString MacSingleKeyShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    const QString reason = SingleKeyShortcutBinder::unsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        return reason;
    }
    const PhysicalKey *key = physicalKey(binding.keyCode());
    if (key->mac < 0) {
        return QStringLiteral("Mac keyboards have no %1 key.")
            .arg(QString::fromLatin1(key->label));
    }
    if (!AXIsProcessTrusted()) {
        return accessibilityRequiredReason();
    }
    return {};
}

QString MacSingleKeyShortcutBinder::watch(const PhysicalKey &key)
{
    unwatch();
    // setShortcut() already refused an untrusted binding; this is the last
    // line of defence against installing a monitor that would never fire.
    if (!AXIsProcessTrusted()) {
        return accessibilityRequiredReason();
    }

    const unsigned short target = static_cast<unsigned short>(key.mac);
    const std::optional<NSUInteger> flagBit = modifierFlagBit(key.mac);
    const NSEventMask mask =
        flagBit ? NSEventMaskFlagsChanged : (NSEventMaskKeyDown | NSEventMaskKeyUp);

    void (^observe)(NSEvent *) = ^(NSEvent *event) {
        if (event.keyCode != target || postedBySpeecher(event)) {
            return;
        }
        // Caps Lock's flag bit is the lock state: it flips on every press and
        // never on release, so the down/up mapping below would activate on
        // every other press only. Treat each flags-changed event as one full
        // press. A hold cannot be observed for a lock key, so push-to-talk
        // degrades to a tap on Caps Lock.
        if (target == kVK_CapsLock) {
            QMetaObject::invokeMethod(
                this,
                [this] {
                    keyDown();
                    keyUp();
                },
                Qt::QueuedConnection);
            return;
        }
        bool down;
        if (flagBit) {
            down = (event.modifierFlags & *flagBit) != 0;
        } else {
            if (event.type == NSEventTypeKeyDown && [event isARepeat]) {
                return;
            }
            down = event.type == NSEventTypeKeyDown;
        }
        // The monitors fire on the main runloop, which is Qt's main thread,
        // but marshal explicitly rather than trusting AppKit's delivery.
        QMetaObject::invokeMethod(
            this, [this, down] { down ? keyDown() : keyUp(); }, Qt::QueuedConnection);
    };

    // A global monitor only sees events dispatched to other applications; the
    // local twin covers presses while Speecher itself is frontmost, and must
    // pass on what it sees rather than consume it.
    id globalMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask handler:observe];
    id localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                            handler:^NSEvent *(NSEvent *event) {
                                                                observe(event);
                                                                return event;
                                                            }];
    if (!globalMonitor || !localMonitor) {
        if (globalMonitor) {
            [NSEvent removeMonitor:globalMonitor];
        }
        if (localMonitor) {
            [NSEvent removeMonitor:localMonitor];
        }
        return QStringLiteral("macOS refused the key-event monitor for %1.")
            .arg(QString::fromLatin1(key.label));
    }
    m_globalMonitor = [globalMonitor retain];
    m_localMonitor = [localMonitor retain];
    return {};
}

void MacSingleKeyShortcutBinder::unwatch()
{
    if (m_globalMonitor) {
        id monitor = static_cast<id>(m_globalMonitor);
        [NSEvent removeMonitor:monitor];
        [monitor release];
        m_globalMonitor = nullptr;
    }
    if (m_localMonitor) {
        id monitor = static_cast<id>(m_localMonitor);
        [NSEvent removeMonitor:monitor];
        [monitor release];
        m_localMonitor = nullptr;
    }
}

} // namespace speecher
