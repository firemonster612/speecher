import QtQuick
import Speecher.Theme

// What a list shows when it has nothing in it yet. Centred under its icon
// for a page's list or search results; compact for a list on a card, where
// it reads as a row: the title and help on the row inset, actions under them.
Column {
    id: empty

    property string iconName: "inbox"
    property string title
    property string text
    property bool compact: false
    default property alias actions: actionRow.data

    readonly property real _textWidth: compact ? width - Theme.rowInset * 2 : Math.min(width - 40, 340)
    function _x(item) { return compact ? Theme.rowInset : (width - item.width) / 2 }

    width: parent ? parent.width : implicitWidth
    topPadding: compact ? Theme.space4 : 28
    bottomPadding: topPadding
    spacing: compact ? 3 : 10

    Icon { id: icon; visible: !empty.compact; x: empty._x(icon); name: empty.iconName; size: 22; color: Theme.textTertiary }
    Txt { id: heading; visible: text.length > 0; x: empty._x(heading); text: empty.title; variant: "label" }
    Txt {
        id: help
        visible: text.length > 0
        x: empty._x(help)
        width: empty._textWidth
        horizontalAlignment: empty.compact ? Text.AlignLeft : Text.AlignHCenter
        text: empty.text
        variant: "small"
        tone: "secondary"
        wrapMode: Text.WordWrap
    }
    Row { id: actionRow; x: empty._x(actionRow); spacing: Theme.space2; topPadding: empty.compact ? Theme.space2 : Theme.space1 }
}
