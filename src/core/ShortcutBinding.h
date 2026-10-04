#pragma once

#include <QKeySequence>
#include <QString>

namespace speecher {

// One row of the physical-key vocabulary: the W3C KeyboardEvent.code name,
// what the UI calls that key, and the platform keycodes. X11 keycodes are
// evdev + 8, so X11 needs no column of its own. The mac column holds the
// macOS virtual keycode (Carbon's kVK_* values, which are positional like the
// code names); -1 marks a key Mac keyboards do not have. The win column holds
// the Windows scancode: the set-1 make code, with 0xE0 in the high byte for
// extended keys, as WM_KEYDOWN's lParam spells it; -1 marks a key that never
// reaches Windows as a scancode.
struct PhysicalKey {
    const char *code;
    const char *label;
    int evdev;
    int mac;
    int win;
};

// The row for a KeyboardEvent.code name, or nullptr when no key has that name.
const PhysicalKey *physicalKey(const QString &code);
// The row for a Linux evdev keycode, or nullptr when the vocabulary lacks it.
const PhysicalKey *physicalKeyForEvdev(int evdev);
// The row for a macOS virtual keycode, or nullptr when the vocabulary lacks it.
const PhysicalKey *physicalKeyForMac(int mac);
// The row for a Windows scancode, or nullptr when the vocabulary lacks it.
const PhysicalKey *physicalKeyForWin(int win);

// The Global Shortcuts: the one that toggles dictation, and the optional
// Cancel Shortcut and Pause Shortcut.
enum class GlobalShortcutRole {
    Dictation,
    Cancel,
    Pause,
};

inline constexpr GlobalShortcutRole globalShortcutRoles[] = {
    GlobalShortcutRole::Dictation,
    GlobalShortcutRole::Cancel,
    GlobalShortcutRole::Pause,
};

// The Global Shortcut: a key combination, which every desktop shortcut service
// accepts, or one physical key, which none does and a platform backend has to
// observe itself. QKeySequence cannot name a single key: Qt folds Alt_L and
// Alt_R into Qt::Key_Alt, and a sequence holding only a modifier is not a chord.
class ShortcutBinding {
public:
    ShortcutBinding() = default;
    ShortcutBinding(const QKeySequence &combination);
    // An unknown code name yields an empty binding.
    static ShortcutBinding singleKey(const QString &code);
    // Reads both toString() forms and the QKeySequence text older installs stored.
    static ShortcutBinding fromString(const QString &text);

    bool isEmpty() const;
    bool isSingleKey() const;
    // A combination with no modifier but Shift: a key that types or that apps
    // act on, such as C or Escape. Taking one for good would take it from
    // every app, so only a shortcut held just while it is needed can use one.
    bool isBareKey() const;
    // Whether both name the same keys. One backend may store F13 as a single
    // key while another stores it as a combination without modifiers; those
    // are the same physical key.
    bool sameKeysAs(const ShortcutBinding &other) const;
    // Empty for a single key.
    QKeySequence combination() const;
    // The KeyboardEvent.code name; empty for a combination.
    QString keyCode() const;
    QString displayText() const;
    QString toString() const;

    bool operator==(const ShortcutBinding &other) const = default;

private:
    QKeySequence m_combination;
    QString m_keyCode;
};

// What binding this key costs, or empty for a key that carries no text: a key
// that types keeps typing after it is bound, which the user has to be told.
// Inline and non-blocking on every platform's recorder.
QString singleKeyTypingWarning(const ShortcutBinding &binding);

} // namespace speecher
