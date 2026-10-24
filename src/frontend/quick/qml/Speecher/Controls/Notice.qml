import QtQuick
import Speecher.Theme

// A line that says why something needs attention or is greyed out, with
// room for the action that fixes it. indicator: dot (a dot in the tone's
// colour), icon (an icon in it) or none. Inside a card set inset to line up
// with the rows; ruled draws an inset divider above it when it follows
// another row, as SettingRow does. actionText adds one small button; put
// anything else in the default slot.
Item {
    id: notice

    property string text
    property string tone: "warning"      // info, warning, negative, positive
    property string indicator: "dot"
    property string iconName: tone === "warning" ? "triangle-alert" : tone === "negative" ? "circle-alert"
                            : tone === "positive" ? "circle-check" : "info"
    property string variant: "body"      // body or small
    property int inset: 0
    property bool ruled: true
    property bool first: Positioner.isFirstItem
    property string actionText
    default property alias actions: actionRow.data
    signal action()

    readonly property color _color: tone === "info" ? Theme.textSecondary : Theme.tone(tone)
    readonly property int _textX: inset + (indicator === "icon" ? 26 : indicator === "dot" ? 17 : 0)

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(variant === "body" ? 48 : 0, label.implicitHeight + 24, actionRow.height + 16)

    Rectangle {
        visible: notice.ruled && !notice.first
        x: notice.inset
        width: parent.width - notice.inset * 2
        height: 1
        color: Theme.rule
    }
    Rectangle {
        visible: notice.indicator === "dot"
        x: notice.inset
        y: label.y + (notice.variant === "body" ? 6 : 5)
        width: 7
        height: 7
        radius: 3.5
        color: notice._color
    }
    Icon {
        visible: notice.indicator === "icon"
        x: notice.inset
        y: label.y + 1
        name: notice.iconName
        size: 15
        color: notice._color
    }
    Txt {
        id: label
        x: notice._textX
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - x - notice.inset - (actionRow.width > 0 ? actionRow.width + 20 : 0)
        text: notice.text
        variant: notice.variant
        wrapMode: Text.WordWrap
    }
    Row {
        id: actionRow
        anchors.right: parent.right
        anchors.rightMargin: notice.inset
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Button {
            visible: notice.actionText.length > 0
            size: "small"
            text: notice.actionText
            onClicked: notice.action()
        }
    }
}
