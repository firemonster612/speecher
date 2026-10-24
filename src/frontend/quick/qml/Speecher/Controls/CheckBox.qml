import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A check box whose tick pops in.
T.CheckBox {
    id: control
    implicitWidth: indicator.width + (text.length ? 8 + label.implicitWidth : 0)
    implicitHeight: 20
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: 8

    indicator: Rectangle {
        id: box
        width: 18
        height: 18
        y: (control.height - height) / 2
        radius: Theme.radiusSmall
        color: control.checked ? Theme.accent : control.hovered ? Theme.fillStrong : Theme.fill
        border.width: control.checked ? 0 : 1
        border.color: Theme.borderStrong
        Behavior on color { EaseColor {} }
        Icon {
            anchors.centerIn: parent
            name: "check"
            size: 13
            color: Theme.accentText
            scale: control.checked ? 1 : Theme.motionScale(0.3)
            opacity: control.checked ? 1 : 0
            Behavior on scale { Ease { duration: Theme.base; curve: Theme.springy } }
            Behavior on opacity { Ease { duration: Theme.fast } }
        }
        FocusRing { target: box; shown: control.visualFocus }
    }
    contentItem: Txt {
        id: label
        leftPadding: box.width + control.spacing
        text: control.text
        variant: "label"
        verticalAlignment: Text.AlignVCenter
    }
}
