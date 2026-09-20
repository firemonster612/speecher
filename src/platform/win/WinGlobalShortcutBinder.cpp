#include "platform/win/WinGlobalShortcutBinder.h"

#include "core/settings/SettingsKeys.h"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QSettings>

namespace speecher {
namespace {

constexpr int firstHotKeyId = 0x5350;
constexpr int secondHotKeyId = 0x5351;
constexpr auto messageWindowClass = L"SpeecherShortcutRawInput";

// The one sentence opener that marks a conflict with another application, so
// describesConflict and the message that reports it agree by construction.
QString conflictErrorPrefix()
{
    return QStringLiteral("Another application already owns ");
}

const QHash<int, quint32> &fixedVirtualKeys()
{
    static const QHash<int, quint32> keys{
        {Qt::Key_Space, VK_SPACE},
        {Qt::Key_Return, VK_RETURN},
        {Qt::Key_Enter, VK_RETURN},
        {Qt::Key_Escape, VK_ESCAPE},
        {Qt::Key_Tab, VK_TAB},
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
    if (const quint32 fixed = fixedVirtualKeys().value(key)) {
        return fixed;
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
    for (auto it = fixedVirtualKeys().cbegin(); it != fixedVirtualKeys().cend(); ++it) {
        if (it.value() == key) {
            return it.key();
        }
    }
    const UINT character = MapVirtualKeyW(key, MAPVK_VK_TO_CHAR);
    if ((character & 0xFFFF) < 0x20) {
        return Qt::Key_unknown;
    }
    return QChar(char16_t(character & 0xFFFF)).toUpper().unicode();
}

QKeySequence savedShortcut()
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    // The stored value can be a single key ("key:…"), which belongs to the
    // single-key binder and must not parse as a sequence here.
    const ShortcutBinding stored =
        ShortcutBinding::fromString(settings.value(SettingsKeys::GlobalShortcut).toString());
    return stored.combination().isEmpty() ? WinGlobalShortcutBinder::defaultShortcut()
                                          : stored.combination();
}

void storeShortcut(const QKeySequence &shortcut)
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    settings.setValue(SettingsKeys::GlobalShortcut, shortcut.toString());
}

} // namespace

QKeySequence WinGlobalShortcutBinder::defaultShortcut()
{
    return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D);
}

WinGlobalShortcutBinder::WinGlobalShortcutBinder(QObject *parent)
    : GlobalShortcutBinder(parent)
    , m_shortcut(savedShortcut())
{
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
    if (m_messageWindow) {
        DestroyWindow(m_messageWindow);
    }
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
    if (!registerShortcut(shortcut.combination(), error)) {
        return false;
    }
    if (m_suspensionCount > 0) {
        // Validate conflicts now, but leave keys available to other recorders.
        m_resumeBinding = true;
        unregisterShortcut();
    }
    m_shortcut = shortcut.combination();
    storeShortcut(m_shortcut);
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
WinGlobalShortcutBinder::nativeHotKey(const QKeySequence &shortcut, QString *error)
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
    if (!(qtModifiers & supportedModifiers)) {
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
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (eventType != QByteArrayLiteral("windows_dispatcher_MSG")
        && eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }
    if (nativeMessage->message == WM_HOTKEY && int(nativeMessage->wParam) == m_hotKeyId) {
        qInfo() << "Global Shortcut pressed";
        m_pressed = true;
        m_pressedKey = HIWORD(nativeMessage->lParam);
        emit activated();
    }
    return false;
}

bool WinGlobalShortcutBinder::registerShortcut(const QKeySequence &shortcut, QString *error)
{
    const auto hotKey = nativeHotKey(shortcut, error);
    if (!hotKey || !ensureMessageWindow(error)) {
        return false;
    }
    if (m_hotKeyId && shortcut == m_shortcut) {
        return true;
    }

    const int newId = m_hotKeyId == firstHotKeyId ? secondHotKeyId : firstHotKeyId;
    if (!RegisterHotKey(nullptr, newId, hotKey->modifiers, hotKey->virtualKey)) {
        if (error) {
            *error = conflictErrorPrefix()
                + shortcut.toString(QKeySequence::NativeText);
        }
        return false;
    }

    RAWINPUTDEVICE keyboard{0x01, 0x06, RIDEV_INPUTSINK, m_messageWindow};
    if (!RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard))) {
        UnregisterHotKey(nullptr, newId);
        if (error) {
            *error = QStringLiteral("Windows could not watch for the Global Shortcut release");
        }
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
    // Raw input remains registered so an outstanding press still receives
    // its release while recording a replacement shortcut.
}

bool WinGlobalShortcutBinder::ensureMessageWindow(QString *error)
{
    if (m_messageWindow) {
        return true;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = messageWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = messageWindowClass;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error) {
            *error = QStringLiteral("Windows could not create the Global Shortcut listener");
        }
        return false;
    }
    m_messageWindow = CreateWindowExW(0,
                                      messageWindowClass,
                                      L"",
                                      0,
                                      0,
                                      0,
                                      0,
                                      0,
                                      HWND_MESSAGE,
                                      nullptr,
                                      instance,
                                      this);
    if (!m_messageWindow && error) {
        *error = QStringLiteral("Windows could not create the Global Shortcut listener");
    }
    return m_messageWindow;
}

LRESULT CALLBACK WinGlobalShortcutBinder::messageWindowProc(HWND window,
                                                             UINT message,
                                                             WPARAM wParam,
                                                             LPARAM lParam)
{
    auto *binder = reinterpret_cast<WinGlobalShortcutBinder *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        binder = static_cast<WinGlobalShortcutBinder *>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(binder));
    } else if (message == WM_INPUT && binder) {
        binder->handleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void WinGlobalShortcutBinder::handleRawInput(HRAWINPUT handle)
{
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
        return;
    }
    handleRawInput(input);
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

} // namespace speecher
