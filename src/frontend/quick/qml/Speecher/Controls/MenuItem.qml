import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// One entry in a menu or a Select: an optional icon, the label with an
// optional badge, a line of help, and a check when it is the current one.
T.ItemDelegate {
    id: item

    property string iconName
    property string help
    property string badge
    property bool current: false

    implicitWidth: leftPadding + rightPadding + (iconName.length ? 24 : 0) + labelRow.implicitWidth + 24
    implicitHeight: Math.max(34, textColumn.implicitHeight + 14)
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    Accessible.role: Accessible.MenuItem
    Accessible.checked: current

    background: Rectangle {
        radius: Theme.radiusLarge - Theme.menuInset
        color: item.down ? Theme.fillStrong : item.highlighted || item.hovered ? Theme.fill : "transparent"
        Behavior on color { EaseColor {} }
    }
    contentItem: Item {
        Icon {
            id: glyph
            visible: item.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: item.iconName
            size: 15
        }
        Column {
            id: textColumn
            x: glyph.visible ? 24 : 0
            width: parent.width - x - 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Row {
                id: labelRow
                spacing: 6
                Txt { text: item.text; variant: "label" }
                Badge { visible: item.badge.length > 0; text: item.badge; anchors.verticalCenter: parent.verticalCenter }
            }
            Txt {
                visible: item.help.length > 0
                text: item.help
                variant: "small"
                tone: "secondary"
                width: Math.min(implicitWidth, parent.width)
                wrapMode: Text.WordWrap
            }
        }
        Icon {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            name: "check"
            size: 15
            color: Theme.accent
            opacity: item.current ? 1 : 0
            scale: item.current ? 1 : Theme.motionScale(0.4)
            Behavior on opacity { Ease { duration: Theme.fast } }
            Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
        }
    }
}
