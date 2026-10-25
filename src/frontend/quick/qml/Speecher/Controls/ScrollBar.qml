import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A thin overlay scroll bar that appears while scrolling or hovered.
// persistent keeps a narrower one in view, for a list that should show it
// runs on, such as a menu's; the list's rows end gutter before it.
T.ScrollBar {
    id: control
    property bool persistent: false
    readonly property real gutter: visible ? width : 0
    implicitWidth: persistent ? 10 : 12
    padding: 3
    minimumSize: 0.08
    hoverEnabled: true
    visible: size < 1

    contentItem: Rectangle {
        implicitWidth: control.hovered || control.pressed ? 6 : 4
        radius: width / 2
        color: control.pressed ? Theme.textTertiary : control.persistent ? Theme.borderStrong : Theme.fillStrong
        opacity: control.persistent || control.active || control.hovered ? 1 : 0
        Behavior on opacity { Ease { duration: Theme.slow } }
        Behavior on implicitWidth { Ease { duration: Theme.fast } }
    }
}
