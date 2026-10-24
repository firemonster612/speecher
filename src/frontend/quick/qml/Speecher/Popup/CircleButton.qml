import QtQuick
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls

// A round icon button for the popup's capsule.
T.Button {
    id: control
    property string iconName
    property string tooltip
    property int size: 30

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.name: tooltip

    scale: Theme.motionScale(down ? 0.88 : hovered ? 1.06 : 1)
    Behavior on scale { Ease { duration: Theme.base; curve: Theme.springy } }

    background: Rectangle {
        radius: width / 2
        color: control.down ? Theme.fillStrong : control.hovered ? Theme.fillStrong : Theme.fill
        border.width: 1
        border.color: Theme.rule
        Behavior on color { EaseColor {} }
    }
    contentItem: Icon {
        name: control.iconName
        size: Math.round(control.size * 0.5)
        color: control.hovered ? Theme.text : Theme.textSecondary
    }
    Tip { text: control.tooltip; shown: control.hovered }
}
