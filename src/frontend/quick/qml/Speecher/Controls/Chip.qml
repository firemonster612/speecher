import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A small button laid out among others in a Flow: one of a set of choices
// (checkable, such as a profile's tone), or an action such as inserting a
// snippet. Checked ones take the blue pencil.
T.Button {
    id: chip

    property string iconName

    implicitHeight: 30
    implicitWidth: content.implicitWidth + 24
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: checkable ? Accessible.CheckBox : Accessible.Button

    scale: down ? Theme.motionScale(0.95) : 1
    Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }

    background: Rectangle {
        id: bg
        radius: Theme.radius
        color: chip.checked ? Theme.accentSoft : chip.hovered ? Theme.fill : "transparent"
        border.width: 1
        border.color: chip.checked ? Theme.accent : chip.hovered ? Theme.borderStrong : Theme.border
        Behavior on color { EaseColor { duration: Theme.base } }
        Behavior on border.color { EaseColor { duration: Theme.base } }
        FocusRing { target: bg; shown: chip.visualFocus }
    }
    contentItem: Item {
        Row {
            id: content
            anchors.centerIn: parent
            spacing: 6
            Icon {
                visible: chip.iconName.length > 0
                name: chip.iconName
                size: 14
                color: chip.checked ? Theme.accent : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }
            Txt {
                text: chip.text
                variant: "labelSmall"
                color: chip.checked ? Theme.accent : Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
