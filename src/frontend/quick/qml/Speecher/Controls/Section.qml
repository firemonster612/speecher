import QtQuick
import Speecher.Theme

// A titled group on a page: the title and caption above, then the rows on a
// raised card. plain drops the card for content that brings its own
// surface; cardPadding insets content that is not rows. A title or caption
// that changes rolls or fades to its new wording.
//
// Two sections side by side line up when each takes the other's natural
// sizes as its minimums: their titles share the top line and their cards
// end together.
//   Section { id: a; minimumHeaderHeight: b.naturalHeaderHeight; minimumCardHeight: b.naturalCardHeight }
Column {
    id: section

    property string title
    property string caption
    property bool plain: false
    property alias cardPadding: card.padding
    property real minimumHeaderHeight: 0
    property real minimumCardHeight: 0
    readonly property real naturalHeaderHeight: _hasHeader ? Math.max(head.implicitHeight, actionSlot.childrenRect.height) : 0
    readonly property real naturalCardHeight: card.implicitHeight
    default property alias content: card.content
    property alias actions: actionSlot.data

    readonly property bool _hasHeader: title.length > 0 || caption.length > 0 || actionSlot.children.length > 0

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space2

    Item {
        visible: section._hasHeader
        width: parent.width
        height: Math.max(section.naturalHeaderHeight, section.minimumHeaderHeight)
        Column {
            id: head
            x: 2
            width: parent.width - actionSlot.childrenRect.width - Theme.space3
            // On its own a title sits on the card; paired, it hangs from the top.
            anchors.top: section.minimumHeaderHeight > 0 ? parent.top : undefined
            anchors.bottom: section.minimumHeaderHeight > 0 ? undefined : parent.bottom
            spacing: 2
            MorphText { text: section.title; variant: "heading"; visible: text.length > 0; width: parent.width; wrap: true }
            MorphText { text: section.caption; variant: "small"; tone: "secondary"; visible: text.length > 0; width: parent.width; wrap: true }
        }
        Row {
            id: actionSlot
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 6
        }
    }

    Card {
        id: card
        flat: section.plain
        height: Math.max(implicitHeight, section.minimumCardHeight)
    }
}
