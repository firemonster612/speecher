import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The menu that stops keeping desktop accessibility on past sign-out, for a
// row showing the desktopAccessibility check: it folds away where Speecher
// keeps nothing on, or where offered is false, as on a list's other rows.
Reveal {
    property bool offered: true
    sideways: true
    open: offered && System.canDisablePermanentAccessibility
    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
    IconButton {
        iconName: "ellipsis"
        size: 28
        tooltip: Words.more
        onClicked: menu.open()
        Menu {
            id: menu
            width: 240
            items: [{ id: "disable", label: "Stop keeping on", icon: "power-off",
                      help: "Turns off when you sign out. Chromium apps stop once restarted." }]
            onPicked: System.disablePermanentAccessibility()
        }
    }
}
