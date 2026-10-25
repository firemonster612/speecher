pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// Release notes in groups, such as Added, Changed and Fixed: the group's
// title on the left, its notes on the right, an inset divider between
// groups. groups: [{ title, items: [string] }].
Column {
    id: root

    property var groups: []
    // Where the notes start, leaving the titles a column of their own.
    readonly property int _notesX: 112

    width: parent ? parent.width : implicitWidth

    Repeater {
        model: root.groups
        Item {
            id: group
            required property var modelData
            required property int index

            width: root.width
            implicitHeight: notes.implicitHeight + Theme.space3 * 2

            Rectangle {
                visible: group.index > 0
                x: Theme.rowInset
                width: parent.width - Theme.rowInset * 2
                height: 1
                color: Theme.rule
            }
            Txt { x: Theme.rowInset; y: Theme.space3; text: group.modelData.title; variant: "label"; tone: "secondary" }
            BulletList {
                id: notes
                x: root._notesX
                y: Theme.space3
                width: parent.width - x - Theme.rowInset
                items: group.modelData.items
            }
        }
    }
}
