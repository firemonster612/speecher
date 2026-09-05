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

QKeySequence defaultShortcut()
{
    return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D);
}

const QHash<int, quint32> &virtualKeys()
{
    static const QHash<int, quint32> keys{
        {Qt::Key_Space, VK_SPACE},
        {Qt::Key_Return, VK_RETURN},
        {Qt::Key_Enter, VK_RETURN},
        {Qt::Key_Escape, VK_ESCAPE},
        {Qt::Key_Tab, VK_TAB},
        {Qt::Key_Minus, VK_OEM_MINUS},
        {Qt::Key_Equal, VK_OEM_PLUS},
        {Qt::Key_BracketLeft, VK_OEM_4},
        {Qt::Key_BracketRight, VK_OEM_6},
        {Qt::Key_Backslash, VK_OEM_5},
        {Qt::Key_Semicolon, VK_OEM_1},
        {Qt::Key_Apostrophe, VK_OEM_7},
        {Qt::Key_Comma, VK_OEM_COMMA},
        {Qt::Key_Period, VK_OEM_PERIOD},
        {Qt::Key_Slash, VK_OEM_2},
        {Qt::Key_QuoteLeft, VK_OEM_3},
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
    return virtualKeys().value(key);
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
    for (auto it = virtualKeys().cbegin(); it != virtualKeys().cend(); ++it) {
        if (it.value() == key) {
            return it.key();
        }
    }
    return Qt::Key_unknown;
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
    QString error;
    if (!registerShortcut(m_shortcut, &error)) {
        qWarning().noquote() << "Could not register the Global Shortcut:" << error;
    }
}

QKeySequence WinGlobalShortcutBinder::shortcut() const
{
    return m_shortcut;
}

bool WinGlobalShortcutBinder::setShortcut(const QKeySequence &shortcut, QString *error)
{
    if (!registerShortcut(shortcut, error)) {
        return false;
    }
    m_shortcut = shortcut;
    storeShortcut(shortcut);
    emit bindingChanged();
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
            *error = QStringLiteral("Another application already owns %1")
                         .arg(shortcut.toString(QKeySequence::NativeText));
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
    m_virtualKey = hotKey->virtualKey;
    return true;
}

void WinGlobalShortcutBinder::unregisterShortcut()
{
    if (m_hotKeyId) {
        UnregisterHotKey(nullptr, m_hotKeyId);
        m_hotKeyId = 0;
    }
    m_pressed = false;
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
    if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != size
        || input.header.dwType != RIM_TYPEKEYBOARD
        || !(input.data.keyboard.Flags & RI_KEY_BREAK)
        || input.data.keyboard.VKey != m_virtualKey
        || !m_pressed) {
        return;
    }
    m_pressed = false;
    emit deactivated();
}

} // namespace speecher
