import QtQuick
import Speecher.Theme

// What a list shows when it has nothing in it yet.
Column {
    id: empty

    property string iconName: "inbox"
    property string title
    property string text
    default property alias actions: actionRow.data

    width: parent ? parent.width : implicitWidth
    topPadding: 28
    bottomPadding: 28
    spacing: 10

    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: empty.iconName; size: 22; color: Theme.textTertiary }
    Txt { anchors.horizontalCenter: parent.horizontalCenter; text: empty.title; variant: "label" }
    Txt {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 40, 340)
        horizontalAlignment: Text.AlignHCenter
        text: empty.text
        variant: "small"
        tone: "secondary"
        wrapMode: Text.WordWrap
    }
    Row { id: actionRow; anchors.horizontalCenter: parent.horizontalCenter; spacing: 8; topPadding: 4 }
}
