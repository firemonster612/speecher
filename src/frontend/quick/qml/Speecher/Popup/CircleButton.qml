import QtQuick
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls

// A round icon button for the popup's capsule. It presses like an
// IconButton; it never takes focus, so the dictation keeps it, and the
// popup's keys do the same jobs.
T.Button {
    id: control
    property string iconName
    property string tooltip
    property int size: 30

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    Accessible.name: tooltip

    scale: down ? Theme.motionScale(0.92) : 1
    Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }

    background: Rectangle {
        radius: width / 2
        color: control.down ? Theme.surfacePressed : control.hovered ? Theme.fillStrong : Theme.fill
        border.width: 1
        border.color: Theme.rule
        Behavior on color { EaseColor {} }
    }
    contentItem: Icon {
        name: control.iconName
        size: Math.round(control.size * 0.5)
        color: control.hovered ? Theme.text : Theme.textSecondary
        Behavior on color { EaseColor {} }
    }
    Tip { text: control.tooltip; shown: control.hovered }
}
