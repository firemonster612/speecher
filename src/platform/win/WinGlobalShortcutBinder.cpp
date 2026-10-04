#include "platform/win/WinGlobalShortcutBinder.h"

#include "core/settings/SettingsKeys.h"
#include "platform/win/WinRawKeyboard.h"

#include <QCoreApplication>
#include <QDebug>
#include <QList>
#include <QSettings>

namespace speecher {
namespace {

// Each binder takes two hot-key ids from here, alternating between them so a
// replacement registers before the old one goes; a session shortcut binder
// takes one. Every binder shares the thread's hot-key table, so their ids
// must differ.
int nextHotKeyId = 0x5350;

// The one sentence opener that marks a conflict with another application, so
// describesConflict and the message that reports it agree by construction.
QString conflictErrorPrefix()
{
    return QStringLiteral("Another application already owns ");
}

// The WM_HOTKEY a native event carries, or nullptr for any other message.
const MSG *hotKeyMessage(const QByteArray &eventType, void *message)
{
    if (eventType != QByteArrayLiteral("windows_dispatcher_MSG")
        && eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return nullptr;
    }
    const auto *nativeMessage = static_cast<const MSG *>(message);
    return nativeMessage->message == WM_HOTKEY ? nativeMessage : nullptr;
}

// Registers keys under id in this thread's hot-key table; the error says
// whether Windows cannot register them at all or another app owns them.
bool registerHotKey(int id, const QKeySequence &keys, bool bareKeyAllowed, QString *error)
{
    const auto hotKey = WinGlobalShortcutBinder::nativeHotKey(keys, error, bareKeyAllowed);
    if (!hotKey) {
        return false;
    }
    if (!RegisterHotKey(nullptr, id, hotKey->modifiers, hotKey->virtualKey)) {
        if (error) {
            *error = conflictErrorPrefix() + keys.toString(QKeySequence::NativeText);
        }
        return false;
    }
    return true;
}

// Named keys and their virtual keys. Ordered: Return comes before Enter, so
// a virtual key reads back as the first key that names it.
struct FixedKey {
    int qtKey;
    quint32 virtualKey;
};

const QList<FixedKey> &fixedVirtualKeys()
{
    static const QList<FixedKey> keys{
        {Qt::Key_Space, VK_SPACE},
        {Qt::Key_Return, VK_RETURN},
        {Qt::Key_Enter, VK_RETURN},
        {Qt::Key_Escape, VK_ESCAPE},
        {Qt::Key_Tab, VK_TAB},
        {Qt::Key_Insert, VK_INSERT},
        {Qt::Key_Delete, VK_DELETE},
        {Qt::Key_Home, VK_HOME},
        {Qt::Key_End, VK_END},
        {Qt::Key_PageUp, VK_PRIOR},
        {Qt::Key_PageDown, VK_NEXT},
        {Qt::Key_Left, VK_LEFT},
        {Qt::Key_Right, VK_RIGHT},
        {Qt::Key_Up, VK_UP},
        {Qt::Key_Down, VK_DOWN},
    };
    return keys;
}

quint32 virtualKeyForQtKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return 'A' + quint32(key - Qt::Key_A);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return '0' + quint32(key - Qt::Key_0);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return VK_F1 + quint32(key - Qt::Key_F1);
    }
    for (const FixedKey &fixed : fixedVirtualKeys()) {
        if (fixed.qtKey == key) {
            return fixed.virtualKey;
        }
    }
    // Punctuation sits on different virtual keys per layout (German + is US
    // =), and the recorders map through the active layout. Qt names printable
    // keys by their unshifted character, so ask the layout for that character.
    if (key < 0x20 || key > 0xFFFF) {
        return 0;
    }
    const SHORT scan = VkKeyScanW(wchar_t(key));
    return scan == -1 ? 0 : quint32(scan & 0xFF);
}

int qtKeyForVirtualKey(quint32 key)
{
    if (key >= 'A' && key <= 'Z') {
        return Qt::Key_A + int(key - 'A');
    }
    if (key >= '0' && key <= '9') {
        return Qt::Key_0 + int(key - '0');
    }
    if (key >= VK_F1 && key <= VK_F24) {
        return Qt::Key_F1 + int(key - VK_F1);
    }
    for (const FixedKey &fixed : fixedVirtualKeys()) {
        if (fixed.virtualKey == key) {
            return fixed.qtKey;
        }
    }
    const UINT character = MapVirtualKeyW(key, MAPVK_VK_TO_CHAR);
    if ((character & 0xFFFF) < 0x20) {
        return Qt::Key_unknown;
    }
    return QChar(char16_t(character & 0xFFFF)).toUpper().unicode();
}

QKeySequence savedShortcut(const GlobalShortcutAction &action)
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    // The stored value can be a single key ("key:…"), which belongs to the
    // single-key binder and must not parse as a sequence here.
    const ShortcutBinding stored =
        ShortcutBinding::fromString(settings.value(action.settingsKey).toString());
    return stored.combination().isEmpty() ? action.defaultShortcut : stored.combination();
}

void storeShortcut(const QString &key, const QKeySequence &shortcut)
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    settings.setValue(key, shortcut.toString());
}

} // namespace

WinGlobalShortcutBinder::WinGlobalShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : GlobalShortcutBinder(std::move(action), parent)
    , m_shortcut(savedShortcut(this->action()))
    , m_firstHotKeyId(nextHotKeyId)
{
    nextHotKeyId += 2;
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WinGlobalShortcutBinder::~WinGlobalShortcutBinder()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    unregisterShortcut();
}

bool WinGlobalShortcutBinder::supported() const
{
    return true;
}

QString WinGlobalShortcutBinder::unsupportedReason() const
{
    return {};
}

void WinGlobalShortcutBinder::bind()
{
    if (m_shortcut.isEmpty()) {
        return;
    }
    if (m_suspensionCount > 0) {
        m_resumeBinding = true;
        return;
    }
    QString error;
    if (!registerShortcut(m_shortcut, &error)) {
        qWarning().noquote() << "Could not register the Global Shortcut:" << error;
    }
}

ShortcutBinding WinGlobalShortcutBinder::shortcut() const
{
    return m_shortcut;
}

bool WinGlobalShortcutBinder::setShortcut(const ShortcutBinding &shortcut, QString *error)
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
        emit bindingChanged();
        return true;
    }
    if (!registerShortcut(shortcut.combination(), error)) {
        return false;
    }
    if (m_suspensionCount > 0) {
        // Validate conflicts now, but leave keys available to other recorders.
        m_resumeBinding = true;
        unregisterShortcut();
    }
    m_shortcut = shortcut.combination();
    storeShortcut(action().settingsKey, m_shortcut);
    emit bindingChanged();
    return true;
}

// A RegisterHotKey chord is consumed system-wide and never arrives as an app
// key event, so recording it (or any replacement) needs the registration gone.
void WinGlobalShortcutBinder::suspend()
{
    if (m_suspensionCount++ > 0) {
        return;
    }
    m_resumeBinding = m_hotKeyId != 0;
    unregisterShortcut();
}

QString WinGlobalShortcutBinder::resume()
{
    if (m_suspensionCount == 0 || --m_suspensionCount > 0) {
        return {};
    }
    QString error;
    if (m_resumeBinding) {
        m_resumeBinding = false;
        registerShortcut(m_shortcut, &error);
    }
    return error;
}

// The router parks this binder while a single key holds the binding; without
// letting go of the hot key here, the replaced combination would keep firing
// alongside the key. Clearing m_resumeBinding keeps a recording's resume from
// sneaking it back.
bool WinGlobalShortcutBinder::removeRegistration(QString *)
{
    m_resumeBinding = false;
    unregisterShortcut();
    return true;
}

std::optional<WinGlobalShortcutBinder::NativeHotKey>
WinGlobalShortcutBinder::nativeHotKey(const QKeySequence &shortcut, QString *error,
                                      bool bareKeyAllowed)
{
    if (shortcut.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Choose a key sequence");
        }
        return std::nullopt;
    }
    if (shortcut.count() != 1) {
        if (error) {
            *error = QStringLiteral("A Windows Global Shortcut must contain exactly one key combination");
        }
        return std::nullopt;
    }

    const QKeyCombination combination = shortcut[0];
    const Qt::KeyboardModifiers qtModifiers = combination.keyboardModifiers();
    const Qt::KeyboardModifiers supportedModifiers = Qt::ControlModifier
        | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;
    if (!(qtModifiers & supportedModifiers) && !bareKeyAllowed) {
        if (error) {
            *error = QStringLiteral("A Windows Global Shortcut must include at least one modifier key");
        }
        return std::nullopt;
    }

    const quint32 key = virtualKeyForQtKey(combination.key());
    if (!key || key == VK_F12) {
        if (error) {
            *error = QStringLiteral("%1 is not a key Windows can register as a Global Shortcut")
                         .arg(shortcut.toString(QKeySequence::NativeText));
        }
        return std::nullopt;
    }

    quint32 modifiers = MOD_NOREPEAT;
    if (qtModifiers & Qt::ControlModifier) {
        modifiers |= MOD_CONTROL;
    }
    if (qtModifiers & Qt::AltModifier) {
        modifiers |= MOD_ALT;
    }
    if (qtModifiers & Qt::ShiftModifier) {
        modifiers |= MOD_SHIFT;
    }
    if (qtModifiers & Qt::MetaModifier) {
        modifiers |= MOD_WIN;
    }
    return NativeHotKey{modifiers, key};
}

bool WinGlobalShortcutBinder::describesConflict(const QString &error)
{
    return error.startsWith(conflictErrorPrefix());
}

QKeySequence WinGlobalShortcutBinder::keySequenceForHotKey(quint32 modifiers,
                                                            quint32 virtualKey)
{
    Qt::KeyboardModifiers qtModifiers;
    if (modifiers & MOD_CONTROL) {
        qtModifiers |= Qt::ControlModifier;
    }
    if (modifiers & MOD_ALT) {
        qtModifiers |= Qt::AltModifier;
    }
    if (modifiers & MOD_SHIFT) {
        qtModifiers |= Qt::ShiftModifier;
    }
    if (modifiers & MOD_WIN) {
        qtModifiers |= Qt::MetaModifier;
    }
    const int key = qtKeyForVirtualKey(virtualKey);
    return key == Qt::Key_unknown ? QKeySequence() : QKeySequence(qtModifiers | Qt::Key(key));
}

bool WinGlobalShortcutBinder::nativeEventFilter(const QByteArray &eventType,
                                                 void *message,
                                                 qintptr *result)
{
    Q_UNUSED(result);
    const MSG *hotKey = hotKeyMessage(eventType, message);
    if (hotKey && int(hotKey->wParam) == m_hotKeyId) {
        qInfo() << "Global Shortcut pressed";
        m_pressed = true;
        m_pressedKey = HIWORD(hotKey->lParam);
        emit activated();
    }
    return false;
}

bool WinGlobalShortcutBinder::registerShortcut(const QKeySequence &shortcut, QString *error)
{
    if (!nativeHotKey(shortcut, error)) {
        return false;
    }
    // The release, which WM_HOTKEY never reports, arrives as raw input.
    if (!win::listenToRawKeyboard(this, [this](const RAWINPUT &input) { handleRawInput(input); },
                                  error)) {
        return false;
    }
    if (m_hotKeyId && shortcut == m_shortcut) {
        return true;
    }

    const int newId = m_hotKeyId == m_firstHotKeyId ? m_firstHotKeyId + 1 : m_firstHotKeyId;
    if (!registerHotKey(newId, shortcut, false, error)) {
        return false;
    }

    unregisterShortcut();
    m_hotKeyId = newId;
    return true;
}

void WinGlobalShortcutBinder::unregisterShortcut()
{
    if (m_hotKeyId) {
        UnregisterHotKey(nullptr, m_hotKeyId);
        m_hotKeyId = 0;
    }
    // The raw input listener stays so an outstanding press still receives
    // its release while recording a replacement shortcut.
}

void WinGlobalShortcutBinder::handleRawInput(const RAWINPUT &input)
{
    if (input.header.dwType != RIM_TYPEKEYBOARD
        || !(input.data.keyboard.Flags & RI_KEY_BREAK)
        || input.data.keyboard.VKey != m_pressedKey
        || !m_pressed) {
        return;
    }
    m_pressed = false;
    emit deactivated();
}

WinSessionShortcutBinder::WinSessionShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : SessionShortcutBinder(std::move(action), parent)
    , m_hotKeyId(nextHotKeyId++)
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
}

WinSessionShortcutBinder::~WinSessionShortcutBinder()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    letGo();
}

bool WinSessionShortcutBinder::supported() const
{
    return true;
}

QString WinSessionShortcutBinder::unsupportedReason() const
{
    return {};
}

// What Windows cannot register at all is refused here, before the base takes
// the keys, so a failed take() means only that another app owns them.
QString WinSessionShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    if (binding.isEmpty() || binding.isSingleKey()) {
        return GlobalShortcutBinder::unsupportedBindingReason(binding);
    }
    QString error;
    WinGlobalShortcutBinder::nativeHotKey(binding.combination(), &error, true);
    return error;
}

bool WinSessionShortcutBinder::nativeEventFilter(const QByteArray &eventType,
                                                 void *message,
                                                 qintptr *result)
{
    Q_UNUSED(result);
    const MSG *hotKey = hotKeyMessage(eventType, message);
    if (hotKey && m_registered && int(hotKey->wParam) == m_hotKeyId) {
        emit activated();
    }
    return false;
}

bool WinSessionShortcutBinder::take(const QKeySequence &keys)
{
    m_registered = registerHotKey(m_hotKeyId, keys, true, nullptr);
    return m_registered;
}

void WinSessionShortcutBinder::letGo()
{
    if (m_registered) {
        UnregisterHotKey(nullptr, m_hotKeyId);
        m_registered = false;
    }
}

} // namespace speecher
