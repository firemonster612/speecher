import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A horizontal slider. The handle grows under the pointer and a bubble shows
// the value while dragging. format turns the value into its label.
T.Slider {
    id: control

    property var format: v => String(Math.round(v))

    implicitWidth: 200
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    background: Item {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        Rectangle { anchors.fill: parent; radius: 2; color: Theme.fillStrong }
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        id: handle
        readonly property real grow: control.pressed ? 1.25 : control.hovered ? 1.1 : 1
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 16
        height: 16
        radius: 8
        scale: Theme.motionScale(grow)
        color: Theme.raised
        border.width: 1
        border.color: Theme.knobEdge
        Behavior on scale { Ease { duration: Theme.base; curve: Theme.springy } }
        FocusRing { target: handle; shown: control.visualFocus }

        TipBody {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -height - (control.pressed ? 8 * Theme.movement : 0)
            text: control.format(control.value)
            opacity: control.pressed ? 1 : 0
            scale: 1 / handle.scale
            Behavior on opacity { Ease { duration: Theme.fast } }
            Behavior on y { Ease {} }
        }
    }
}
