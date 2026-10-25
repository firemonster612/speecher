import QtQuick

// Web-style :focus-visible for a custom control that takes focus when it is
// clicked, so its arrow keys work afterwards: shown is true while the control
// has focus that came from the keyboard, or once the person starts using
// keys on it, and never just because it was clicked. The control calls
// pointed() before forceActiveFocus() in its pointer handlers and keyed() in
// its key handlers. Templates controls get the same from visualFocus.
Item {
    id: focusVisible
    readonly property Item control: parent
    readonly property bool shown: control !== null && control.activeFocus && !_byPointer
    property bool _byPointer: false

    function pointed() { _byPointer = true }
    function keyed() { _byPointer = false }

    Connections {
        target: focusVisible.control
        function onActiveFocusChanged() { if (!focusVisible.control.activeFocus) focusVisible._byPointer = false }
    }
}
