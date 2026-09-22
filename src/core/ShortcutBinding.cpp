#include "core/ShortcutBinding.h"

#include <QLatin1StringView>

#ifdef Q_OS_LINUX
#include "setup/KeywatchProtocol.h"
#endif

namespace speecher {
namespace {

#if defined(Q_OS_MACOS)
#define SPEECHER_CONTROL_NAME "Control"
#define SPEECHER_ALT_NAME "Option"
#define SPEECHER_META_NAME "Command"
#elif defined(Q_OS_WIN)
#define SPEECHER_CONTROL_NAME "Ctrl"
#define SPEECHER_ALT_NAME "Alt"
#define SPEECHER_META_NAME "Win"
#else
#define SPEECHER_CONTROL_NAME "Ctrl"
#define SPEECHER_ALT_NAME "Alt"
#define SPEECHER_META_NAME "Meta"
#endif

// No macOS virtual keycode: the key does not exist on Mac keyboards, so the
// macOS backend refuses the binding rather than watching a wrong key.
constexpr int noMac = -1;
// No Windows scancode: Fn is handled inside the keyboard and never reaches
// the OS, so the Windows backend refuses the binding rather than never firing.
constexpr int noWin = -1;

// KeyboardEvent.code names, which tell left from right and have published
// mappings to evdev codes, Windows scancodes and macOS virtual key codes. The
// evdev column is input-event-codes.h; Fn (KEY_FN) sits past the X11 keycode
// range, which the X11 backend reports rather than watching a wrong key. The
// mac column is Carbon's kVK_* values (HIToolbox/Events.h), spelled as numbers
// because this file also builds where no Carbon header exists. The win column
// is the message-level scancode (Chromium's dom_code_data.inc win column):
// Pause is 0x45 and NumLock 0xE045 there, and the raw-input backend normalizes
// its own E1/E0 spelling of those two keys to match. All columns are
// positional, so a layout change moves none of them.
constexpr PhysicalKey physicalKeys[] = {
    {"ShiftLeft", "Left Shift", 42, 56, 0x2A},
    {"ShiftRight", "Right Shift", 54, 60, 0x36},
    {"ControlLeft", "Left " SPEECHER_CONTROL_NAME, 29, 59, 0x1D},
    {"ControlRight", "Right " SPEECHER_CONTROL_NAME, 97, 62, 0xE01D},
    {"AltLeft", "Left " SPEECHER_ALT_NAME, 56, 58, 0x38},
    {"AltRight", "Right " SPEECHER_ALT_NAME, 100, 61, 0xE038},
    {"MetaLeft", "Left " SPEECHER_META_NAME, 125, 55, 0xE05B},
    {"MetaRight", "Right " SPEECHER_META_NAME, 126, 54, 0xE05C},
    {"CapsLock", "Caps Lock", 58, 57, 0x3A},
    {"Fn", "Fn", 464, 63, noWin},
    {"F1", "F1", 59, 122, 0x3B}, {"F2", "F2", 60, 120, 0x3C}, {"F3", "F3", 61, 99, 0x3D}, {"F4", "F4", 62, 118, 0x3E}, {"F5", "F5", 63, 96, 0x3F}, {"F6", "F6", 64, 97, 0x40},
    {"F7", "F7", 65, 98, 0x41}, {"F8", "F8", 66, 100, 0x42}, {"F9", "F9", 67, 101, 0x43}, {"F10", "F10", 68, 109, 0x44}, {"F11", "F11", 87, 103, 0x57}, {"F12", "F12", 88, 111, 0x58},
    {"F13", "F13", 183, 105, 0x64}, {"F14", "F14", 184, 107, 0x65}, {"F15", "F15", 185, 113, 0x66}, {"F16", "F16", 186, 106, 0x67}, {"F17", "F17", 187, 64, 0x68}, {"F18", "F18", 188, 79, 0x69},
    {"F19", "F19", 189, 80, 0x6A}, {"F20", "F20", 190, 90, 0x6B}, {"F21", "F21", 191, noMac, 0x6C}, {"F22", "F22", 192, noMac, 0x6D}, {"F23", "F23", 193, noMac, 0x6E}, {"F24", "F24", 194, noMac, 0x76},
    {"KeyA", "A", 30, 0, 0x1E}, {"KeyB", "B", 48, 11, 0x30}, {"KeyC", "C", 46, 8, 0x2E}, {"KeyD", "D", 32, 2, 0x20}, {"KeyE", "E", 18, 14, 0x12}, {"KeyF", "F", 33, 3, 0x21},
    {"KeyG", "G", 34, 5, 0x22}, {"KeyH", "H", 35, 4, 0x23}, {"KeyI", "I", 23, 34, 0x17}, {"KeyJ", "J", 36, 38, 0x24}, {"KeyK", "K", 37, 40, 0x25}, {"KeyL", "L", 38, 37, 0x26},
    {"KeyM", "M", 50, 46, 0x32}, {"KeyN", "N", 49, 45, 0x31}, {"KeyO", "O", 24, 31, 0x18}, {"KeyP", "P", 25, 35, 0x19}, {"KeyQ", "Q", 16, 12, 0x10}, {"KeyR", "R", 19, 15, 0x13},
    {"KeyS", "S", 31, 1, 0x1F}, {"KeyT", "T", 20, 17, 0x14}, {"KeyU", "U", 22, 32, 0x16}, {"KeyV", "V", 47, 9, 0x2F}, {"KeyW", "W", 17, 13, 0x11}, {"KeyX", "X", 45, 7, 0x2D},
    {"KeyY", "Y", 21, 16, 0x15}, {"KeyZ", "Z", 44, 6, 0x2C},
    {"Digit0", "0", 11, 29, 0x0B}, {"Digit1", "1", 2, 18, 0x02}, {"Digit2", "2", 3, 19, 0x03}, {"Digit3", "3", 4, 20, 0x04}, {"Digit4", "4", 5, 21, 0x05},
    {"Digit5", "5", 6, 23, 0x06}, {"Digit6", "6", 7, 22, 0x07}, {"Digit7", "7", 8, 26, 0x08}, {"Digit8", "8", 9, 28, 0x09}, {"Digit9", "9", 10, 25, 0x0A},
    {"Space", "Space", 57, 49, 0x39},
    {"Enter", "Enter", 28, 36, 0x1C},
    {"Tab", "Tab", 15, 48, 0x0F},
    {"Escape", "Escape", 1, 53, 0x01},
    {"Backspace", "Backspace", 14, 51, 0x0E},
    {"Minus", "-", 12, 27, 0x0C}, {"Equal", "=", 13, 24, 0x0D}, {"BracketLeft", "[", 26, 33, 0x1A}, {"BracketRight", "]", 27, 30, 0x1B},
    {"Backslash", "\\", 43, 42, 0x2B}, {"Semicolon", ";", 39, 41, 0x27}, {"Quote", "'", 40, 39, 0x28}, {"Backquote", "`", 41, 50, 0x29},
    {"Comma", ",", 51, 43, 0x33}, {"Period", ".", 52, 47, 0x34}, {"Slash", "/", 53, 44, 0x35},
    {"IntlBackslash", "International \\", 86, 10, 0x56}, {"IntlRo", "Ro", 89, 94, 0x73}, {"IntlYen", "Yen", 124, 93, 0x7D},
    {"Insert", "Insert", 110, 114, 0xE052}, {"Delete", "Delete", 111, 117, 0xE053}, {"Home", "Home", 102, 115, 0xE047}, {"End", "End", 107, 119, 0xE04F},
    {"PageUp", "Page Up", 104, 116, 0xE049}, {"PageDown", "Page Down", 109, 121, 0xE051},
    {"ArrowUp", "Up", 103, 126, 0xE048}, {"ArrowDown", "Down", 108, 125, 0xE050}, {"ArrowLeft", "Left", 105, 123, 0xE04B}, {"ArrowRight", "Right", 106, 124, 0xE04D},
    {"PrintScreen", "Print Screen", 99, noMac, 0xE037}, {"ScrollLock", "Scroll Lock", 70, noMac, 0x46}, {"Pause", "Pause", 119, noMac, 0x45},
    {"ContextMenu", "Menu", 127, 110, 0xE05D}, {"NumLock", "Num Lock", 69, 71, 0xE045},
    {"Numpad0", "Numpad 0", 82, 82, 0x52}, {"Numpad1", "Numpad 1", 79, 83, 0x4F}, {"Numpad2", "Numpad 2", 80, 84, 0x50},
    {"Numpad3", "Numpad 3", 81, 85, 0x51}, {"Numpad4", "Numpad 4", 75, 86, 0x4B}, {"Numpad5", "Numpad 5", 76, 87, 0x4C},
    {"Numpad6", "Numpad 6", 77, 88, 0x4D}, {"Numpad7", "Numpad 7", 71, 89, 0x47}, {"Numpad8", "Numpad 8", 72, 91, 0x48},
    {"Numpad9", "Numpad 9", 73, 92, 0x49},
    {"NumpadAdd", "Numpad +", 78, 69, 0x4E}, {"NumpadSubtract", "Numpad -", 74, 78, 0x4A}, {"NumpadMultiply", "Numpad *", 55, 67, 0x37},
    {"NumpadDivide", "Numpad /", 98, 75, 0xE035}, {"NumpadDecimal", "Numpad .", 83, 65, 0x53}, {"NumpadEnter", "Numpad Enter", 96, 76, 0xE01C},
    {"NumpadEqual", "Numpad =", 117, 81, 0x59}, {"NumpadComma", "Numpad ,", 121, 95, 0x7E},
};

#undef SPEECHER_CONTROL_NAME
#undef SPEECHER_ALT_NAME
#undef SPEECHER_META_NAME

#ifdef Q_OS_LINUX
// The daemon's allowlist repeats code<->evdev pairs from this table inside its
// privileged boundary, and the binder validates by code name only: a
// mismatched edit would make the daemon silently watch a different physical
// key than the one the user recorded. Keep the copies provably in sync.
constexpr bool permittedKeysMatchPhysicalKeys()
{
    for (const keywatch::PermittedKey &permitted : keywatch::permittedKeys) {
        bool matched = false;
        for (const PhysicalKey &key : physicalKeys) {
            if (permitted.code == key.code) {
                matched = int(permitted.evdev) == key.evdev;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}
static_assert(permittedKeysMatchPhysicalKeys(),
              "keywatch::permittedKeys disagrees with physicalKeys about a key's evdev code");
#endif

// Distinguishes a stored single key from the QKeySequence text older installs hold.
constexpr auto singleKeyPrefix = "key:";

} // namespace

const PhysicalKey *physicalKey(const QString &code)
{
    for (const PhysicalKey &key : physicalKeys) {
        if (code == QLatin1StringView(key.code)) {
            return &key;
        }
    }
    return nullptr;
}

const PhysicalKey *physicalKeyForEvdev(int evdev)
{
    for (const PhysicalKey &key : physicalKeys) {
        if (key.evdev == evdev) {
            return &key;
        }
    }
    return nullptr;
}

const PhysicalKey *physicalKeyForMac(int mac)
{
    if (mac == noMac) {
        return nullptr;
    }
    for (const PhysicalKey &key : physicalKeys) {
        if (key.mac == mac) {
            return &key;
        }
    }
    return nullptr;
}

const PhysicalKey *physicalKeyForWin(int win)
{
    if (win == noWin) {
        return nullptr;
    }
    for (const PhysicalKey &key : physicalKeys) {
        if (key.win == win) {
            return &key;
        }
    }
    return nullptr;
}

ShortcutBinding::ShortcutBinding(const QKeySequence &combination)
    : m_combination(combination)
{
}

ShortcutBinding ShortcutBinding::singleKey(const QString &code)
{
    ShortcutBinding binding;
    if (physicalKey(code)) {
        binding.m_keyCode = code;
    }
    return binding;
}

ShortcutBinding ShortcutBinding::fromString(const QString &text)
{
    const QLatin1StringView prefix(singleKeyPrefix);
    if (text.startsWith(prefix)) {
        return singleKey(text.mid(prefix.size()));
    }
    return ShortcutBinding(QKeySequence(text));
}

bool ShortcutBinding::isEmpty() const
{
    return m_combination.isEmpty() && m_keyCode.isEmpty();
}

bool ShortcutBinding::isSingleKey() const
{
    return !m_keyCode.isEmpty();
}

QKeySequence ShortcutBinding::combination() const
{
    return m_combination;
}

QString ShortcutBinding::keyCode() const
{
    return m_keyCode;
}

QString ShortcutBinding::displayText() const
{
    if (const PhysicalKey *key = physicalKey(m_keyCode)) {
        return QString::fromLatin1(key->label);
    }
    return m_combination.toString(QKeySequence::NativeText);
}

QString ShortcutBinding::toString() const
{
    if (isSingleKey()) {
        return QLatin1StringView(singleKeyPrefix) + m_keyCode;
    }
    return m_combination.toString();
}

// Modifiers, Caps Lock, Fn and the F1-F24 block carry no text, so they bind
// without a caveat.
QString singleKeyTypingWarning(const ShortcutBinding &binding)
{
    static const char *const silentPrefixes[] = {"Shift", "Control", "Alt", "Meta",
                                                 "CapsLock", "Fn"};
    const QString code = binding.keyCode();
    for (const char *prefix : silentPrefixes) {
        if (code.startsWith(QLatin1StringView(prefix))) {
            return QString();
        }
    }
    if (code.startsWith(QLatin1Char('F')) && code.size() > 1 && code.at(1).isDigit()) {
        return QString();
    }
    return QStringLiteral(
        "Heads up: %1 still does its normal job and now also starts dictation, "
        "so pressing it types as well.")
        .arg(binding.displayText());
}

} // namespace speecher
