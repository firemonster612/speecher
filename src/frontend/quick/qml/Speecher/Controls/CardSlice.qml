import QtQuick
import Speecher.Theme

// One row's stretch of a Card, for a list that builds its rows one at a
// time (Page's listDelegate), where no one Card can hold them. Slices
// stacked read as one card: first and last give it its rounded ends, and
// between them the card runs on, edges and shadow unbroken. Its rows go in
// the default slot.
Item {
    id: slice

    property bool first: true
    property bool last: true
    default property alias content: rows.data

    // Room for the shadow past the card's edges, and how far past a slice's
    // top or bottom the card is drawn where it goes on, to hide its rounding
    // there.
    readonly property int _shadowRoom: 8
    readonly property int _past: Theme.radiusLarge + _shadowRoom

    width: parent ? parent.width : implicitWidth
    implicitHeight: rows.implicitHeight

    // Where the card goes on, it reaches a pixel under the next slice,
    // which draws over it, so a slice at a fraction of a pixel leaves no
    // seam.
    Item {
        x: -slice._shadowRoom
        y: slice.first ? -slice._shadowRoom : 0
        width: slice.width + slice._shadowRoom * 2
        height: slice.height - y + (slice.last ? slice._shadowRoom : 1)
        clip: true

        Card {
            x: slice._shadowRoom
            y: (slice.first ? 0 : -slice._past) - parent.y
            width: slice.width
            height: slice.height + (slice.first ? 0 : slice._past) + (slice.last ? 0 : slice._past)
        }
    }

    Column {
        id: rows
        width: parent.width
    }
}
