pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// Short notes as a bulleted list, each wrapping under its own text, such as
// release notes. Each item is a line of Markdown (RichText.inlineMarkdown), so
// `code` and links show as such; a link opens in the browser.
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
                text: RichText.inlineMarkdown(entry.modelData, Theme.monoFamily, Theme.accent)
                variant: list.variant
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                onLinkActivated: link => Qt.openUrlExternally(link)
                HoverHandler { cursorShape: note.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
        }
    }
}
