pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// A copy button that shows a tick for a moment after it is pressed. With
// no text it is an icon button (give it a tooltip); with text it is a
// Button whose label rolls to copiedText. copied fires on the press: the
// caller puts the text on the clipboard.
Item {
    id: root

    property string text
    property string tooltip
    property string copiedText
    property string size: "medium"       // small or medium, as Button's
    property string variant: "secondary" // a labelled button's variant
    signal copied()

    property bool _done: false
    function _press() { copied(); _done = true; reset.restart() }

    implicitWidth: loader.implicitWidth
    implicitHeight: loader.implicitHeight

    Loader {
        id: loader
        sourceComponent: root.text.length ? labelled : iconOnly
    }
    Component {
        id: iconOnly
        IconButton {
            size: root.size === "small" ? 24 : 30
            iconName: root._done ? "check" : "copy"
            iconColor: root._done ? Theme.accent : hovered ? Theme.text : Theme.textSecondary
            tooltip: root._done ? "" : root.tooltip
            onClicked: root._press()
            Tip { text: root.copiedText; shown: root._done; delay: 0 }
        }
    }
    Component {
        id: labelled
        Button {
            text: root._done ? root.copiedText : root.text
            iconName: root._done ? "check" : "copy"
            size: root.size
            variant: root.variant
            onClicked: root._press()
        }
    }
    Timer { id: reset; interval: Theme.ms(1600); onTriggered: root._done = false }
}
