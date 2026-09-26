#include "platform/mac/MacGlobalShortcutBinder.h"

#include "core/settings/SettingsKeys.h"
#include "platform/mac/MacKeyCode.h"

#include <QDebug>
#include <QHash>
#include <QSettings>

#import <Carbon/Carbon.h>

#include <optional>

namespace speecher {
namespace {

constexpr UInt32 hotKeySignature = 'spch';
constexpr UInt32 hotKeyIdentifier = 1;

QKeySequence defaultShortcut()
{
    return QKeySequence(Qt::META | Qt::ALT | Qt::Key_D);
}

// Function and control keys have fixed positions. Printable keys must follow
// the current input source because QKeySequence stores logical characters.
std::optional<UInt32> carbonKeyCode(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    static const QHash<int, UInt32> fixedKeys{
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3},
        {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6},
        {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9},
        {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
        {Qt::Key_Return, kVK_Return}, {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
        {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Space, kVK_Space},
    };
    const auto fixed = fixedKeys.constFind(key);
    if (fixed != fixedKeys.cend()) return *fixed;
    if (key < Qt::Key_Exclam || key > 0xffff) return std::nullopt;

    return mac::keyCodeForCharacter(QChar(static_cast<ushort>(key)), modifiers);
}

bool carbonHotKeyFor(const QKeySequence &shortcut, UInt32 *keyCode, UInt32 *modifiers, QString *error)
{
    if (shortcut.count() != 1) {
        if (error) {
            *error = QStringLiteral("A macOS global shortcut must contain exactly one key combination");
        }
        return false;
    }
    const QKeyCombination combination = shortcut[0];
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
    const Qt::KeyboardModifiers globalModifiers = Qt::ControlModifier | Qt::MetaModifier
        | Qt::AltModifier | Qt::ShiftModifier;
    if (!(qtModifiers & globalModifiers)) {
        if (error) {
            *error = QStringLiteral("A macOS global shortcut must include at least one modifier key");
        }
        return false;
    }
    const auto found = carbonKeyCode(combination.key(), qtModifiers);
    if (!found) {
        if (error) {
            *error = QStringLiteral("%1 is not a key macOS can register as a global shortcut")
                         .arg(QKeySequence(combination).toString(QKeySequence::NativeText));
        }
        return false;
    }
    *keyCode = *found;

    // Qt maps the Mac keyboard onto its portable enum: the Command key arrives as
    // Qt::ControlModifier and the Control key as Qt::MetaModifier.
    *modifiers = 0;
    if (qtModifiers & Qt::ControlModifier) {
        *modifiers |= cmdKey;
    }
    if (qtModifiers & Qt::MetaModifier) {
        *modifiers |= controlKey;
    }
    if (qtModifiers & Qt::AltModifier) {
        *modifiers |= optionKey;
    }
    if (qtModifiers & Qt::ShiftModifier) {
        *modifiers |= shiftKey;
    }
    return true;
}

OSStatus handleHotKeyEvent(EventHandlerCallRef, EventRef event, void *userData)
{
    auto *binder = static_cast<MacGlobalShortcutBinder *>(userData);
    EventHotKeyID pressed;
    if (!binder
        || GetEventParameter(event,
                             kEventParamDirectObject,
                             typeEventHotKeyID,
                             nullptr,
                             sizeof(pressed),
                             nullptr,
                             &pressed) != noErr
        || pressed.signature != hotKeySignature
        || pressed.id != hotKeyIdentifier) {
        return eventNotHandledErr;
    }

    // Carbon dispatches on the main runloop, so a release can be handled long
    // after it happened while the main thread is busy; GetEventTime() is when
    // it happened. One hot key is registered at a time, so one press time is
    // enough.
    static EventTime pressedAt = -1;
    if (GetEventKind(event) == kEventHotKeyPressed) {
        pressedAt = GetEventTime(event);
        emit binder->activated();
    } else {
        const EventTime releasedAt = GetEventTime(event);
        const qint64 heldMs = pressedAt >= 0 && releasedAt >= pressedAt
            ? qint64((releasedAt - pressedAt) * 1000.0)
            : -1;
        pressedAt = -1;
        emit binder->deactivated(heldMs);
    }
    return noErr;
}

QKeySequence savedShortcut()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    // The stored value can be a single key ("key:…"), which belongs to the
    // single-key binder and must not parse as a sequence here.
    const ShortcutBinding stored =
        ShortcutBinding::fromString(settings.value(SettingsKeys::GlobalShortcut).toString());
    return stored.combination().isEmpty() ? defaultShortcut() : stored.combination();
}

void storeShortcut(const QKeySequence &shortcut)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    settings.setValue(SettingsKeys::GlobalShortcut, shortcut.toString());
}

} // namespace

MacGlobalShortcutBinder::MacGlobalShortcutBinder(QObject *parent)
    : GlobalShortcutBinder(parent)
    , m_shortcut(savedShortcut())
{
    CFNotificationCenterAddObserver(
        CFNotificationCenterGetDistributedCenter(), this,
        [](CFNotificationCenterRef, void *observer, CFStringRef, const void *, CFDictionaryRef) {
            auto *binder = static_cast<MacGlobalShortcutBinder *>(observer);
            QMetaObject::invokeMethod(binder, &MacGlobalShortcutBinder::refreshKeyboardLayout,
                                      Qt::QueuedConnection);
        },
        kTISNotifySelectedKeyboardInputSourceChanged, nullptr,
        CFNotificationSuspensionBehaviorDeliverImmediately);
}

MacGlobalShortcutBinder::~MacGlobalShortcutBinder()
{
    CFNotificationCenterRemoveObserver(CFNotificationCenterGetDistributedCenter(), this,
                                        kTISNotifySelectedKeyboardInputSourceChanged, nullptr);
    unregisterHotKey();
    if (m_eventHandler) {
        RemoveEventHandler(static_cast<EventHandlerRef>(m_eventHandler));
        m_eventHandler = nullptr;
    }
    if (m_eventHandlerUpp) {
        DisposeEventHandlerUPP(reinterpret_cast<EventHandlerUPP>(m_eventHandlerUpp));
        m_eventHandlerUpp = nullptr;
    }
}

bool MacGlobalShortcutBinder::supported() const
{
    return true;
}

QString MacGlobalShortcutBinder::unsupportedReason() const
{
    return {};
}

void MacGlobalShortcutBinder::bind()
{
    if (m_suspensionCount > 0) {
        m_resumeBinding = true;
        return;
    }
    QString error;
    if (!registerHotKey(m_shortcut, &error)) {
        qWarning().noquote() << "Could not register the global shortcut:" << error;
    }
}

ShortcutBinding MacGlobalShortcutBinder::shortcut() const
{
    return m_shortcut;
}

bool MacGlobalShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
{
    const QString reason = unsupportedBindingReason(shortcut);
    if (!reason.isEmpty()) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    if (!registerHotKey(shortcut.combination(), error)) {
        return false;
    }
    if (m_suspensionCount > 0) {
        // Validate conflicts now, but leave keys available to other recorders.
        m_resumeBinding = true;
        unregisterHotKey();
    }
    m_shortcut = shortcut.combination();
    storeShortcut(m_shortcut);
    return true;
}

// A Carbon hotkey is consumed system-wide and never arrives as an app key
// event, so recording it (or any replacement) needs the registration gone.
void MacGlobalShortcutBinder::suspend()
{
    if (m_suspensionCount++ > 0) return;
    m_resumeBinding = m_hotKey != nullptr;
    unregisterHotKey();
}

QString MacGlobalShortcutBinder::resume()
{
    if (m_suspensionCount == 0 || --m_suspensionCount > 0) return {};
    QString error;
    if (m_resumeBinding) {
        m_resumeBinding = false;
        registerHotKey(m_shortcut, &error);
    }
    return error;
}

// The router calls this when a single key replaces the combination: the
// Carbon hotkey has to go now, not at the next launch, or both would fire.
bool MacGlobalShortcutBinder::removeRegistration(QString *)
{
    m_resumeBinding = false;
    unregisterHotKey();
    return true;
}

void MacGlobalShortcutBinder::refreshKeyboardLayout()
{
    // A layout change can move an unchanged logical shortcut to another key,
    // but only a live (or suspension-parked) registration should follow it:
    // while a single key holds the binding, the router left this binder
    // unbound and a layout change must not sneak the combination back.
    if (!m_hotKey && !m_resumeBinding) {
        return;
    }
    bind();
}

void MacGlobalShortcutBinder::unregisterHotKey()
{
    if (m_hotKey) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_hotKey));
        m_hotKey = nullptr;
    }
    m_registeredKeyCode = 0;
    m_registeredModifiers = 0;
}

bool MacGlobalShortcutBinder::registerHotKey(const QKeySequence &shortcut, QString *error)
{
    if (shortcut.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Choose a key sequence");
        }
        return false;
    }

    UInt32 keyCode = 0;
    UInt32 modifiers = 0;
    if (!carbonHotKeyFor(shortcut, &keyCode, &modifiers, error)) {
        return false;
    }

    if (!m_eventHandler) {
        static const EventTypeSpec hotKeyEvents[] = {
            {kEventClassKeyboard, kEventHotKeyPressed},
            {kEventClassKeyboard, kEventHotKeyReleased},
        };
        EventHandlerUPP upp = NewEventHandlerUPP(handleHotKeyEvent);
        EventHandlerRef handler = nullptr;
        if (InstallApplicationEventHandler(upp,
                                           GetEventTypeCount(hotKeyEvents),
                                           hotKeyEvents,
                                           this,
                                           &handler) != noErr) {
            DisposeEventHandlerUPP(upp);
            if (error) {
                *error = QStringLiteral("Could not install the global hot-key handler");
            }
            return false;
        }
        m_eventHandler = handler;
        m_eventHandlerUpp = reinterpret_cast<void *>(upp);
    }

    // Layout changes can move an unchanged logical shortcut to another key.
    // Carbon only rejects duplicate hardware key/modifier registrations.
    if (m_hotKey && keyCode == m_registeredKeyCode && modifiers == m_registeredModifiers) {
        return true;
    }

    const EventHotKeyID identifier{hotKeySignature, hotKeyIdentifier};
    EventHotKeyRef hotKey = nullptr;
    if (RegisterEventHotKey(keyCode, modifiers, identifier, GetApplicationEventTarget(), 0, &hotKey)
        != noErr) {
        if (error) {
            *error = QStringLiteral("Another application already owns %1")
                         .arg(shortcut.toString(QKeySequence::NativeText));
        }
        return false;
    }
    // Dropped only now that a replacement exists: unregistering first left the
    // user with no working shortcut whenever the new combination was taken.
    unregisterHotKey();
    m_hotKey = hotKey;
    m_registeredKeyCode = keyCode;
    m_registeredModifiers = modifiers;
    return true;
}

} // namespace speecher
