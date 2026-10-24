import QtQuick
import QtQuick.Effects
import Speecher.Theme

// A floating panel for menus, popovers and dialogs: raised colour, a hairline
// border and a soft shadow.
Item {
    property int radius: Theme.radiusLarge
    property color color: Theme.raised

    RectangularShadow {
        anchors.fill: body
        radius: body.radius
        blur: 32
        spread: -4
        offset.y: 12
        color: Theme.shadow
    }
    Rectangle {
        id: body
        anchors.fill: parent
        radius: parent.radius
        color: parent.color
        border.width: 1
        border.color: Theme.borderStrong
    }
}
