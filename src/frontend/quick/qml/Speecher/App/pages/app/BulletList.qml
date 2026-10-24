pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// Short notes as a bulleted list, each wrapping under its own text: release
// notes and the highlights of an available update.
Column {
    id: list

    property var items: []
    property string variant: "body"

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space2

    Repeater {
        model: list.items
        Item {
            id: entry
            required property string modelData
            width: list.width
            implicitHeight: note.implicitHeight
            Rectangle {
                // Level with the middle of the first line's lowercase letters.
                y: Math.round(note.font.pixelSize * 0.57)
                width: 4
                height: 4
                radius: 2
                color: Theme.textTertiary
            }
            Txt {
                id: note
                x: Theme.space3
                width: parent.width - x
                text: entry.modelData
                variant: list.variant
                wrapMode: Text.WordWrap
            }
        }
    }
}
