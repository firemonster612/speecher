#include "platform/mac/MacGlobalShortcutBinder.h"

#include "core/settings/SettingsKeys.h"
#include "platform/mac/MacKeyCode.h"

#include <QDebug>
#include <QHash>
#include <QSettings>

#import <Carbon/Carbon.h>

#include <functional>
#include <optional>
#include <utility>

namespace speecher {
namespace {

constexpr UInt32 hotKeySignature = 'spch';
// Each hot key registers under its own identifier, so the dictation, Cancel
// and Pause binders each handle only their own presses.
UInt32 nextHotKeyIdentifier = 1;

// Function and control keys have fixed positions. Printable keys must follow
// the current input source because QKeySequence stores logical characters.
std::optional<UInt32> carbonKeyCode(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    static const QHash<int, UInt32> fixedKeys{
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3},
        {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6},
        {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9},
        {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
        {Qt::Key_F13, kVK_F13}, {Qt::Key_F14, kVK_F14}, {Qt::Key_F15, kVK_F15},
        {Qt::Key_F16, kVK_F16}, {Qt::Key_F17, kVK_F17}, {Qt::Key_F18, kVK_F18},
        {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20},
        {Qt::Key_Return, kVK_Return}, {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
        {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Space, kVK_Space},
    };
    const auto fixed = fixedKeys.constFind(key);
    if (fixed != fixedKeys.cend()) return *fixed;
    if (key < Qt::Key_Exclam || key > 0xffff) return std::nullopt;

    return mac::keyCodeForCharacter(QChar(static_cast<ushort>(key)), modifiers);
}

// A bare key is allowed only for a shortcut held just during a session; held
// for good it would take the key from every app.
bool carbonHotKeyFor(const QKeySequence &shortcut,
                     bool bareKeyAllowed,
                     UInt32 *keyCode,
                     UInt32 *modifiers,
                     QString *error)
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
    if (!bareKeyAllowed && !(qtModifiers & globalModifiers)) {
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

QKeySequence savedShortcut(const GlobalShortcutAction &action)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    // The stored value can be a single key ("key:…"), which belongs to the
    // single-key binder and must not parse as a sequence here.
    const ShortcutBinding stored =
        ShortcutBinding::fromString(settings.value(action.settingsKey).toString());
    return stored.combination().isEmpty() ? action.defaultShortcut : stored.combination();
}

void storeShortcut(const QString &key, const QKeySequence &shortcut)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    settings.setValue(key, shortcut.toString());
}

} // namespace

// Registers one combination as a Carbon hot key and reports its presses and
// releases. While registered it follows input source changes, which can move
// an unchanged logical shortcut to another physical key.
class CarbonHotKey {
public:
    using Pressed = std::function<void()>;
    using Released = std::function<void(qint64 heldMs)>;

    // context receives the queued input source notifications and must
    // outlive this hot key.
    CarbonHotKey(QObject *context, bool bareKeyAllowed, Pressed pressed, Released released = {})
        : m_context(context)
        , m_bareKeyAllowed(bareKeyAllowed)
        , m_pressed(std::move(pressed))
        , m_released(std::move(released))
        , m_identifier(nextHotKeyIdentifier++)
    {
        CFNotificationCenterAddObserver(
            CFNotificationCenterGetDistributedCenter(), this,
            [](CFNotificationCenterRef, void *observer, CFStringRef, const void *, CFDictionaryRef) {
                auto *hotKey = static_cast<CarbonHotKey *>(observer);
                QMetaObject::invokeMethod(hotKey->m_context, [hotKey] { hotKey->followInputSource(); },
                                          Qt::QueuedConnection);
            },
            kTISNotifySelectedKeyboardInputSourceChanged, nullptr,
            CFNotificationSuspensionBehaviorDeliverImmediately);
    }

    ~CarbonHotKey()
    {
        CFNotificationCenterRemoveObserver(CFNotificationCenterGetDistributedCenter(), this,
                                            kTISNotifySelectedKeyboardInputSourceChanged, nullptr);
        unregister();
        if (m_eventHandler) {
            RemoveEventHandler(m_eventHandler);
        }
        if (m_eventHandlerUpp) {
            DisposeEventHandlerUPP(m_eventHandlerUpp);
        }
    }

    CarbonHotKey(const CarbonHotKey &) = delete;
    CarbonHotKey &operator=(const CarbonHotKey &) = delete;

    // Why Carbon cannot take these keys at all, whoever holds them; empty
    // when it can.
    QString unsupportedReason(const QKeySequence &keys) const
    {
        UInt32 keyCode = 0;
        UInt32 modifiers = 0;
        QString error;
        carbonHotKeyFor(keys, m_bareKeyAllowed, &keyCode, &modifiers, &error);
        return error;
    }

    bool registered() const { return m_hotKey != nullptr; }

    bool registerKeys(const QKeySequence &keys, QString *error)
    {
        if (keys.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Choose a key sequence");
            }
            return false;
        }

        UInt32 keyCode = 0;
        UInt32 modifiers = 0;
        if (!carbonHotKeyFor(keys, m_bareKeyAllowed, &keyCode, &modifiers, error)) {
            return false;
        }

        if (!m_eventHandler && !installEventHandler()) {
            if (error) {
                *error = QStringLiteral("Could not install the global hot-key handler");
            }
            return false;
        }

        // Carbon only rejects duplicate hardware key/modifier registrations.
        if (m_hotKey && keyCode == m_registeredKeyCode && modifiers == m_registeredModifiers) {
            m_keys = keys;
            return true;
        }

        const EventHotKeyID identifier{hotKeySignature, m_identifier};
        EventHotKeyRef hotKey = nullptr;
        if (RegisterEventHotKey(keyCode, modifiers, identifier, GetApplicationEventTarget(), 0, &hotKey)
            != noErr) {
            if (error) {
                *error = QStringLiteral("Another application already owns %1")
                             .arg(keys.toString(QKeySequence::NativeText));
            }
            return false;
        }
        // Dropped only now that a replacement exists: unregistering first left the
        // user with no working shortcut whenever the new combination was taken.
        unregister();
        m_hotKey = hotKey;
        m_keys = keys;
        m_registeredKeyCode = keyCode;
        m_registeredModifiers = modifiers;
        return true;
    }

    void unregister()
    {
        if (m_hotKey) {
            UnregisterEventHotKey(m_hotKey);
            m_hotKey = nullptr;
        }
        m_registeredKeyCode = 0;
        m_registeredModifiers = 0;
    }

private:
    bool installEventHandler()
    {
        static const EventTypeSpec hotKeyEvents[] = {
            {kEventClassKeyboard, kEventHotKeyPressed},
            {kEventClassKeyboard, kEventHotKeyReleased},
        };
        EventHandlerUPP upp = NewEventHandlerUPP(handleHotKeyEvent);
        EventHandlerRef handler = nullptr;
        if (InstallApplicationEventHandler(upp, GetEventTypeCount(hotKeyEvents), hotKeyEvents, this,
                                           &handler)
            != noErr) {
            DisposeEventHandlerUPP(upp);
            return false;
        }
        m_eventHandler = handler;
        m_eventHandlerUpp = upp;
        return true;
    }

    // Only a live registration follows the new layout. One a binder let go
    // of (suspended, unarmed, or replaced by a single key) stays gone.
    void followInputSource()
    {
        if (!m_hotKey) {
            return;
        }
        QString error;
        if (!registerKeys(m_keys, &error)) {
            qWarning().noquote() << "Could not move the global shortcut to the new keyboard layout:"
                                 << error;
        }
    }

    static OSStatus handleHotKeyEvent(EventHandlerCallRef, EventRef event, void *userData)
    {
        auto *hotKey = static_cast<CarbonHotKey *>(userData);
        EventHotKeyID pressed;
        if (!hotKey
            || GetEventParameter(event,
                                 kEventParamDirectObject,
                                 typeEventHotKeyID,
                                 nullptr,
                                 sizeof(pressed),
                                 nullptr,
                                 &pressed) != noErr
            || pressed.signature != hotKeySignature
            || pressed.id != hotKey->m_identifier) {
            return eventNotHandledErr;
        }

        // Carbon dispatches on the main runloop, so a release can be handled long
        // after it happened while the main thread is busy; GetEventTime() is when
        // it happened.
        if (GetEventKind(event) == kEventHotKeyPressed) {
            hotKey->m_pressedAt = GetEventTime(event);
            hotKey->m_pressed();
        } else {
            const EventTime pressedTime = std::exchange(hotKey->m_pressedAt, 0);
            const EventTime releasedAt = GetEventTime(event);
            const qint64 heldMs = pressedTime > 0 && releasedAt >= pressedTime
                ? qint64((releasedAt - pressedTime) * 1000.0)
                : -1;
            if (hotKey->m_released) {
                hotKey->m_released(heldMs);
            }
        }
        return noErr;
    }

    QObject *m_context;
    bool m_bareKeyAllowed;
    Pressed m_pressed;
    Released m_released;
    UInt32 m_identifier;
    QKeySequence m_keys;
    EventHotKeyRef m_hotKey = nullptr;
    UInt32 m_registeredKeyCode = 0;
    UInt32 m_registeredModifiers = 0;
    EventHandlerRef m_eventHandler = nullptr;
    EventHandlerUPP m_eventHandlerUpp = nullptr;
    EventTime m_pressedAt = 0;
};

MacGlobalShortcutBinder::MacGlobalShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
    , m_shortcut(savedShortcut(this->action()))
    , m_hotKey(std::make_unique<CarbonHotKey>(
          this, false, [this] { emit activated(); }, [this](qint64 heldMs) { emit deactivated(heldMs); }))
{
}

MacGlobalShortcutBinder::~MacGlobalShortcutBinder() = default;

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
    if (m_shortcut.isEmpty()) {
        return;
    }
    if (m_suspensionCount > 0) {
        m_resumeBinding = true;
        return;
    }
    QString error;
    if (!m_hotKey->registerKeys(m_shortcut, &error)) {
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
    if (shortcut.isEmpty()) {
        removeRegistration();
        m_shortcut = {};
        storeShortcut(action().settingsKey, m_shortcut);
        return true;
    }
    if (!m_hotKey->registerKeys(shortcut.combination(), error)) {
        return false;
    }
    if (m_suspensionCount > 0) {
        // Validate conflicts now, but leave keys available to other recorders.
        m_resumeBinding = true;
        m_hotKey->unregister();
    }
    m_shortcut = shortcut.combination();
    storeShortcut(action().settingsKey, m_shortcut);
    return true;
}

// A Carbon hotkey is consumed system-wide and never arrives as an app key
// event, so recording it (or any replacement) needs the registration gone.
void MacGlobalShortcutBinder::suspend()
{
    if (m_suspensionCount++ > 0) return;
    m_resumeBinding = m_hotKey->registered();
    m_hotKey->unregister();
}

QString MacGlobalShortcutBinder::resume()
{
    if (m_suspensionCount == 0 || --m_suspensionCount > 0) return {};
    QString error;
    if (m_resumeBinding) {
        m_resumeBinding = false;
        m_hotKey->registerKeys(m_shortcut, &error);
    }
    return error;
}

// The router calls this when a single key replaces the combination: the
// Carbon hotkey has to go now, not at the next launch, or both would fire.
bool MacGlobalShortcutBinder::removeRegistration(QString *)
{
    m_resumeBinding = false;
    m_hotKey->unregister();
    return true;
}

MacSessionShortcutBinder::MacSessionShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : SessionShortcutBinder(std::move(action), parent)
    , m_hotKey(std::make_unique<CarbonHotKey>(this, true, [this] { emit activated(); }))
{
}

MacSessionShortcutBinder::~MacSessionShortcutBinder() = default;

bool MacSessionShortcutBinder::supported() const
{
    return true;
}

QString MacSessionShortcutBinder::unsupportedReason() const
{
    return {};
}

// A key Carbon has no code for is refused as such, not as one another app
// holds, which is all take() could say.
QString MacSessionShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    const QString reason = SessionShortcutBinder::unsupportedBindingReason(binding);
    if (!reason.isEmpty() || binding.isEmpty()) {
        return reason;
    }
    return m_hotKey->unsupportedReason(binding.combination());
}

bool MacSessionShortcutBinder::take(const QKeySequence &keys)
{
    return m_hotKey->registerKeys(keys, nullptr);
}

void MacSessionShortcutBinder::letGo()
{
    m_hotKey->unregister();
}

} // namespace speecher
