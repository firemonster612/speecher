#include "platform/mac/MacGlobalShortcutBinder.h"

#include "core/settings/SettingsKeys.h"

#include <QDebug>
#include <QHash>
#include <QSettings>

#import <Carbon/Carbon.h>

namespace speecher {
namespace {

constexpr UInt32 hotKeySignature = 'spch';
constexpr UInt32 hotKeyIdentifier = 1;

QKeySequence defaultShortcut()
{
    return QKeySequence(Qt::META | Qt::ALT | Qt::Key_D);
}

// Carbon wants raw virtual key codes, which are neither contiguous nor ordered
// like Qt's key enum, so the supported keys are spelled out. Anything outside
// this table is rejected rather than silently bound to the wrong key.
const QHash<int, UInt32> &carbonKeyCodes()
{
    static const QHash<int, UInt32> codes{
        {Qt::Key_A, kVK_ANSI_A}, {Qt::Key_B, kVK_ANSI_B}, {Qt::Key_C, kVK_ANSI_C},
        {Qt::Key_D, kVK_ANSI_D}, {Qt::Key_E, kVK_ANSI_E}, {Qt::Key_F, kVK_ANSI_F},
        {Qt::Key_G, kVK_ANSI_G}, {Qt::Key_H, kVK_ANSI_H}, {Qt::Key_I, kVK_ANSI_I},
        {Qt::Key_J, kVK_ANSI_J}, {Qt::Key_K, kVK_ANSI_K}, {Qt::Key_L, kVK_ANSI_L},
        {Qt::Key_M, kVK_ANSI_M}, {Qt::Key_N, kVK_ANSI_N}, {Qt::Key_O, kVK_ANSI_O},
        {Qt::Key_P, kVK_ANSI_P}, {Qt::Key_Q, kVK_ANSI_Q}, {Qt::Key_R, kVK_ANSI_R},
        {Qt::Key_S, kVK_ANSI_S}, {Qt::Key_T, kVK_ANSI_T}, {Qt::Key_U, kVK_ANSI_U},
        {Qt::Key_V, kVK_ANSI_V}, {Qt::Key_W, kVK_ANSI_W}, {Qt::Key_X, kVK_ANSI_X},
        {Qt::Key_Y, kVK_ANSI_Y}, {Qt::Key_Z, kVK_ANSI_Z},
        {Qt::Key_0, kVK_ANSI_0}, {Qt::Key_1, kVK_ANSI_1}, {Qt::Key_2, kVK_ANSI_2},
        {Qt::Key_3, kVK_ANSI_3}, {Qt::Key_4, kVK_ANSI_4}, {Qt::Key_5, kVK_ANSI_5},
        {Qt::Key_6, kVK_ANSI_6}, {Qt::Key_7, kVK_ANSI_7}, {Qt::Key_8, kVK_ANSI_8},
        {Qt::Key_9, kVK_ANSI_9},
        {Qt::Key_F1, kVK_F1}, {Qt::Key_F2, kVK_F2}, {Qt::Key_F3, kVK_F3},
        {Qt::Key_F4, kVK_F4}, {Qt::Key_F5, kVK_F5}, {Qt::Key_F6, kVK_F6},
        {Qt::Key_F7, kVK_F7}, {Qt::Key_F8, kVK_F8}, {Qt::Key_F9, kVK_F9},
        {Qt::Key_F10, kVK_F10}, {Qt::Key_F11, kVK_F11}, {Qt::Key_F12, kVK_F12},
        {Qt::Key_Space, kVK_Space},
        {Qt::Key_Return, kVK_Return},
        {Qt::Key_Enter, kVK_ANSI_KeypadEnter},
        {Qt::Key_Escape, kVK_Escape},
        {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Minus, kVK_ANSI_Minus},
        {Qt::Key_Equal, kVK_ANSI_Equal},
        {Qt::Key_BracketLeft, kVK_ANSI_LeftBracket},
        {Qt::Key_BracketRight, kVK_ANSI_RightBracket},
        {Qt::Key_Backslash, kVK_ANSI_Backslash},
        {Qt::Key_Semicolon, kVK_ANSI_Semicolon},
        {Qt::Key_Apostrophe, kVK_ANSI_Quote},
        {Qt::Key_Comma, kVK_ANSI_Comma},
        {Qt::Key_Period, kVK_ANSI_Period},
        {Qt::Key_Slash, kVK_ANSI_Slash},
        {Qt::Key_QuoteLeft, kVK_ANSI_Grave},
    };
    return codes;
}

bool carbonHotKeyFor(const QKeySequence &shortcut, UInt32 *keyCode, UInt32 *modifiers, QString *error)
{
    const QKeyCombination combination = shortcut[0];
    const auto found = carbonKeyCodes().constFind(combination.key());
    if (found == carbonKeyCodes().cend()) {
        if (error) {
            *error = QStringLiteral("%1 is not a key macOS can register as a global shortcut")
                         .arg(QKeySequence(combination).toString(QKeySequence::NativeText));
        }
        return false;
    }
    *keyCode = *found;

    // Qt maps the Mac keyboard onto its portable enum: the Command key arrives as
    // Qt::ControlModifier and the Control key as Qt::MetaModifier.
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
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

    if (GetEventKind(event) == kEventHotKeyPressed) {
        emit binder->activated();
    } else {
        emit binder->deactivated();
    }
    return noErr;
}

QKeySequence savedShortcut()
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    const QString stored = settings.value(SettingsKeys::GlobalShortcut).toString();
    return stored.isEmpty() ? defaultShortcut() : QKeySequence(stored);
}

void storeShortcut(const QKeySequence &shortcut)
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    settings.setValue(SettingsKeys::GlobalShortcut, shortcut.toString());
}

} // namespace

MacGlobalShortcutBinder::MacGlobalShortcutBinder(QObject *parent)
    : GlobalShortcutBinder(parent)
    , m_shortcut(savedShortcut())
{
}

MacGlobalShortcutBinder::~MacGlobalShortcutBinder()
{
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
    QString error;
    if (!registerHotKey(m_shortcut, &error)) {
        qWarning().noquote() << "Could not register the global shortcut:" << error;
    }
}

QKeySequence MacGlobalShortcutBinder::shortcut() const
{
    return m_shortcut;
}

bool MacGlobalShortcutBinder::setShortcut(const QKeySequence &shortcut, QString *error)
{
    if (!registerHotKey(shortcut, error)) {
        return false;
    }
    m_shortcut = shortcut;
    storeShortcut(shortcut);
    return true;
}

void MacGlobalShortcutBinder::unregisterHotKey()
{
    if (m_hotKey) {
        UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_hotKey));
        m_hotKey = nullptr;
    }
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

    // Carbon refuses a second registration of the same combination, and the
    // registration already in place is that one.
    if (m_hotKey && shortcut == m_shortcut) {
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
    return true;
}

} // namespace speecher
