import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// Shows a shortcut as key caps in a field's frame, as wide as a Select so
// it lines up with them in a column of rows. Clicking it listens for a new combination,
// which recorded brings back as key names (["Ctrl", "Shift", "F1"]). Escape
// stops listening without recording, unless escapeRecords (for a shortcut
// that may be Escape itself, such as cancel). Tab and Shift+Tab move focus
// on, which stops listening too, as does any other loss of focus or its
// window hiding. While it listens a clear button records an empty shortcut.
// A key with no name in a combination (a modifier, Caps Lock) records what
// keyAlone returns for it when it is the last key pressed and that same
// physical key is let go; on macOS Caps Lock records at once.
// Where something else picks the keys (recordsKeys false), clicking it only
// emits clicked. emptyText, listeningText and clearText are the words it shows.
T.AbstractButton {
    id: control

    property var keys: []
    property string emptyText
    property string listeningText
    property string clearText
    property bool listening: false
    property bool escapeRecords: false
    property bool recordsKeys: true
    // A function the caller gives that returns what recorded brings back for
    // the key just pressed or let go on its own, or [] when it can't stand
    // alone. Only the caller can tell Right Alt from Left; unset, such a key
    // records nothing.
    property var keyAlone: null
    signal recorded(var keys)

    // keyAlone's answer for the nameless key pressed last, recorded when the
    // same physical key is let go.
    property var _alone: []
    // macOS sends Caps Lock once per toggle, a press as the lock turns on and
    // a release as it turns off, never both for one tap.
    readonly property bool _capsLockToggles: Qt.platform.os === "osx"

    function _recordAlone(keys) {
        listening = false
        recorded(keys)
    }

    // The name Qt's key sequences give a key ("Esc", "PgUp", "F1", "A"), or
    // nothing for a key that has none, such as a modifier on its own.
    function keyName(key) {
        if (key >= Qt.Key_F1 && key <= Qt.Key_F35) return "F" + (key - Qt.Key_F1 + 1)
        const named = {
            [Qt.Key_Space]: "Space", [Qt.Key_Escape]: "Esc", [Qt.Key_Tab]: "Tab", [Qt.Key_Backtab]: "Tab",
            [Qt.Key_Backspace]: "Backspace", [Qt.Key_Return]: "Return", [Qt.Key_Enter]: "Enter",
            [Qt.Key_Insert]: "Ins", [Qt.Key_Delete]: "Del", [Qt.Key_Pause]: "Pause", [Qt.Key_Print]: "Print",
            [Qt.Key_Home]: "Home", [Qt.Key_End]: "End", [Qt.Key_PageUp]: "PgUp", [Qt.Key_PageDown]: "PgDown",
            [Qt.Key_Left]: "Left", [Qt.Key_Up]: "Up", [Qt.Key_Right]: "Right", [Qt.Key_Down]: "Down"
        }
        if (named[key]) return named[key]
        // Below 0x01000000 a key code is the character itself, letters in
        // capitals; above it are the named keys, modifiers among them.
        return key > Qt.Key_Space && key < 0x01000000 ? String.fromCodePoint(key) : ""
    }

    implicitHeight: Theme.controlHeight + 2
    implicitWidth: Math.max(Theme.selectWidth, content.width + clear.width + 16)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    onClicked: if (recordsKeys) { listening = true; _alone = []; forceActiveFocus() }

    // While listening, a combination such as Ctrl+K goes to the field rather
    // than to the window's shortcuts.
    Keys.onShortcutOverride: event => event.accepted = listening
    Keys.onPressed: event => {
        if (!listening) return
        const plain = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
        // Left unaccepted, so the window moves focus on.
        if (plain && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) return
        event.accepted = true
        if (event.key === Qt.Key_Escape && !escapeRecords) { listening = false; return }
        const name = keyName(event.key)
        if (!name) {
            _alone = keyAlone ? keyAlone() : []
            if (_capsLockToggles && event.key === Qt.Key_CapsLock && _alone.length > 0) _recordAlone(_alone)
            return
        }
        const mods = []
        if (event.modifiers & Qt.ControlModifier) mods.push("Ctrl")
        if (event.modifiers & Qt.AltModifier) mods.push("Alt")
        if (event.modifiers & Qt.ShiftModifier) mods.push("Shift")
        if (event.modifiers & Qt.MetaModifier) mods.push("Meta")
        listening = false
        recorded(mods.concat([name]))
    }
    Keys.onReleased: event => {
        if (!listening || event.isAutoRepeat) return
        const released = keyAlone ? keyAlone() : []
        const toggle = _capsLockToggles && event.key === Qt.Key_CapsLock
        if (released.length === 0 || (!toggle && String(released) !== String(_alone))) return
        event.accepted = true
        _recordAlone(released)
    }
    onActiveFocusChanged: if (!activeFocus) listening = false
    // Closing a window hides it, and a hidden window keeps its focus item.
    Window.onVisibilityChanged: if (Window.visibility === Window.Hidden) listening = false

    background: Rectangle {
        id: bg
        radius: Theme.radius
        color: control.listening ? Theme.accentSoft : control.down ? Theme.surfacePressed : control.hovered ? Theme.surfaceHover : Theme.surface
        border.width: 1
        border.color: control.listening ? Theme.accent : control.hovered ? Theme.borderStrong : Theme.border
        Behavior on color { EaseColor {} }
        Behavior on border.color { EaseColor {} }
        SequentialAnimation on opacity {
            running: control.listening
            loops: Animation.Infinite
            onStopped: bg.opacity = 1
            Ease { to: 0.6; duration: Theme.ms(700); curve: Theme.breathe }
            Ease { to: 1; duration: Theme.ms(700); curve: Theme.breathe }
        }
        FocusRing { target: bg; shown: control.visualFocus && !control.listening }
    }
    contentItem: Item {
        // The keys, or the prompt while listening, fading between the two.
        Switcher {
            id: content
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: -clear.width / 2
            width: implicitWidth
            travel: 0
            currentIndex: control.listening || control.keys.length === 0 ? 1 : 0
            KeyCombo { keys: control.keys }
            Txt {
                text: control.listening ? control.listeningText : control.emptyText
                tone: control.listening ? "accent" : "secondary"
                variant: "label"
            }
        }
        Reveal {
            id: clear
            sideways: true
            open: control.listening && control.keys.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            // Leaves focus with the field, so listening goes on until the press lands.
            IconButton {
                iconName: "x"
                size: 24
                tooltip: control.clearText
                focusPolicy: Qt.NoFocus
                onClicked: { control.listening = false; control.recorded([]) }
            }
        }
    }
}
