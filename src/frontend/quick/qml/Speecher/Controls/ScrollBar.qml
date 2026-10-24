import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A thin overlay scroll bar that appears while scrolling or hovered.
T.ScrollBar {
    id: control
    implicitWidth: 12
    padding: 3
    minimumSize: 0.08
    hoverEnabled: true
    visible: size < 1

    contentItem: Rectangle {
        implicitWidth: control.hovered || control.pressed ? 6 : 4
        radius: width / 2
        color: control.pressed ? Theme.textTertiary : Theme.fillStrong
        opacity: control.active || control.hovered ? 1 : 0
        Behavior on opacity { Ease { duration: Theme.slow } }
        Behavior on implicitWidth { Ease { duration: Theme.fast } }
    }
}
