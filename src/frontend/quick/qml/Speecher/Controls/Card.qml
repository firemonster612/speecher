import QtQuick
import QtQuick.Effects
import Speecher.Theme

// A group of rows on a raised card, a shade lighter than the page. Rows
// inside draw inset dividers between themselves. flat drops the card for
// content that brings its own surface.
Item {
    id: card
    default property alias content: column.data
    property alias spacing: column.spacing
    property int padding: 0
    property bool flat: false
    property color color: Theme.raised
    property int radius: Theme.radiusLarge

    width: parent ? parent.width : implicitWidth
    implicitHeight: column.implicitHeight + padding * 2

    RectangularShadow {
        visible: !card.flat
        anchors.fill: body
        radius: body.radius
        blur: 3
        offset.y: 1
        color: Theme.cardShadow
    }
    Rectangle {
        id: body
        visible: !card.flat
        anchors.fill: parent
        radius: card.radius
        color: card.color
        border.width: 1
        border.color: Theme.border
    }

    Column {
        id: column
        x: card.padding
        y: card.padding
        width: card.width - card.padding * 2
    }
}
