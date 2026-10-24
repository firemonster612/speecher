import QtQuick
import Speecher.Theme

// A short status word, outlined. tone colours the text and the dot for
// states that need attention (positive, warning, negative, accent);
// neutral stays quiet.
Rectangle {
    id: badge
    property string text
    property string tone: "neutral"
    property bool dot: false
    property string iconName

    readonly property color _c: Theme.tone(tone)

    implicitWidth: row.implicitWidth + 12
    implicitHeight: 20
    radius: Theme.radiusSmall
    color: "transparent"
    border.width: 1
    border.color: tone === "neutral" ? Theme.border : Theme.soft(_c, 0.4)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Rectangle { visible: badge.dot; width: 6; height: 6; radius: 3; color: badge._c; anchors.verticalCenter: parent.verticalCenter }
        Icon { visible: badge.iconName.length > 0; name: badge.iconName; size: 11; color: badge._c; anchors.verticalCenter: parent.verticalCenter }
        Txt { text: badge.text; variant: "caption"; color: badge._c; anchors.verticalCenter: parent.verticalCenter }
    }
}
