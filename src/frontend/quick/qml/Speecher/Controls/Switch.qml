import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// An on/off switch. The knob glides to the other end when it is toggled.
T.Switch {
    id: control

    implicitWidth: 36
    implicitHeight: 22
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }

    indicator: Rectangle {
        id: track
        width: control.width
        height: control.height
        radius: height / 2
        readonly property color shade: control.checked ? (control.hovered ? Theme.accentHover : Theme.accent)
                                                       : (control.hovered ? Theme.fillStrong : Theme.fill)
        // Pressing tints the track toward fillStrong, whichever way it is set.
        color: control.pressed ? Qt.tint(shade, Theme.fillStrong) : shade
        border.width: control.checked ? 0 : 1
        border.color: Theme.border
        Behavior on color { EaseColor {} }

        Rectangle {
            id: knob
            readonly property int inset: 3
            width: track.height - inset * 2
            height: track.height - inset * 2
            radius: height / 2
            y: inset
            x: control.checked ? track.width - width - inset : inset
            color: control.checked ? Theme.sheet : Theme.raised
            // Under reduced motion the knob moves at once.
            Behavior on x { enabled: Theme.movement > 0; Ease { duration: Theme.base } }
            Behavior on color { EaseColor { duration: Theme.base } }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                z: -1
                radius: height / 2
                color: "transparent"
                border.color: Theme.knobEdge
                border.width: 1
                visible: !control.checked
            }
        }
        FocusRing { target: track; shown: control.visualFocus }
    }
    contentItem: Item {}
}
