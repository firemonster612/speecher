import QtQuick
import Speecher.Theme

// Content that opens and closes in place: its height eases between nothing
// and what the content needs, while the content fades and drops in. Use it
// for anything that appears on demand (a form under a list, a confirm
// strip, a section that depends on another setting) instead of toggling
// visible, which pops. Closing keeps the content drawn until it has folded
// away, and content that changes height while open eases to the new size.
// sideways folds the width instead, for a control in a row. Rows inside
// draw their divider unless the Reveal itself starts its card.
Item {
    id: reveal

    property bool open: false
    property bool sideways: false
    default property alias content: body.data
    readonly property bool moving: growHeight.running || growWidth.running

    width: sideways ? implicitWidth : parent ? parent.width : implicitWidth
    height: implicitHeight
    implicitWidth: sideways && !open ? 0 : body.implicitWidth
    implicitHeight: !sideways && !open ? 0 : body.implicitHeight
    visible: width > 0.5 && height > 0.5
    clip: moving || !open
    Grow on implicitHeight { id: growHeight }
    Grow on implicitWidth { id: growWidth }

    Column {
        id: body
        width: reveal.sideways ? implicitWidth : reveal.width
        opacity: reveal.open ? 1 : 0
        transform: Translate {
            x: reveal.sideways && !reveal.open ? -8 * Theme.movement : 0
            y: !reveal.sideways && !reveal.open ? -8 * Theme.movement : 0
            Behavior on x { Ease { duration: Theme.slow } }
            Behavior on y { Ease { duration: Theme.slow } }
        }
        Behavior on opacity { Ease { duration: reveal.open ? Theme.slow : Theme.fast } }

        // A row's first flag comes from its place in this column. This
        // stand-in takes first place when something sits above the Reveal,
        // so the first row inside still draws its divider. A Column skips
        // empty items, so it has a sliver of height.
        Item { width: 1; height: 0.001; visible: !reveal.sideways && reveal.Positioner.index > 0 }
    }
}
