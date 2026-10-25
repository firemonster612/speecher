import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A square button showing only an icon. Give it a tooltip so its purpose is
// never only visual.
T.Button {
    id: control

    property string iconName
    property int size: 30
    property color iconColor: hovered ? Theme.text : Theme.textSecondary
    property string tooltip

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Accessible.name: tooltip

    scale: down ? Theme.motionScale(0.92) : 1
    Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }

    background: Rectangle {
        id: bg
        radius: Theme.radius
        color: control.down ? Theme.fillStrong : control.hovered || control.checked ? Theme.fill : "transparent"
        Behavior on color { EaseColor {} }
        FocusRing { target: bg; shown: control.visualFocus }
    }
    contentItem: Icon {
        name: control.iconName
        size: Math.round(control.size * 0.53)
        color: control.iconColor
    }

    Tip { text: control.tooltip; shown: control.hovered && control.tooltip.length > 0 }
}
