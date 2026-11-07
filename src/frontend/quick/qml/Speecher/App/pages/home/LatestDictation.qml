import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "words.js" as T

// The latest thing said, set large, with where it went and Copy, at the top
// of Your dictation; before the first, where it will appear.
Column {
    id: root
    readonly property var latest: Insights.latest

    width: parent ? parent.width : implicitWidth
    padding: Theme.space5
    topPadding: Theme.space4
    bottomPadding: Theme.space3
    spacing: Theme.space2
    Txt {
        width: root.width - 2 * root.padding
        text: root.latest ? root.latest.text : T.text.nothingYet
        tone: root.latest ? "primary" : "tertiary"
        variant: "lead"
        wrapMode: Text.WordWrap
    }
    Reveal {
        open: root.latest !== null
        width: root.width - 2 * root.padding
        Item {
            width: parent.width
            height: copy.height
            Txt {
                anchors.verticalCenter: parent.verticalCenter
                text: root.latest ? T.latestDetail(root.latest, Insights.today) : ""
                variant: "small"
                tone: "secondary"
            }
            CopyButton {
                id: copy
                anchors.right: parent.right
                anchors.rightMargin: -Theme.space1
                text: T.text.copy
                copiedText: Words.copied
                size: "small"
                variant: "ghost"
                onCopied: Insights.copyLatest()
            }
        }
    }
}
