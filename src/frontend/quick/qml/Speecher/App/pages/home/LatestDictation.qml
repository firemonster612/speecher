import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "words.js" as T

// The latest thing said, set large, with where it went, History and Copy,
// at the top of Your dictation; nothing before the first, where the
// section's own empty row says so.
Column {
    id: root
    readonly property var latest: Insights.latest

    visible: !!latest
    width: parent ? parent.width : implicitWidth
    padding: Theme.space5
    topPadding: Theme.space4
    bottomPadding: Theme.space3
    spacing: Theme.space2
    Txt {
        width: root.width - 2 * root.padding
        text: root.latest ? root.latest.text : ""
        variant: "lead"
        wrapMode: Text.WordWrap
    }
    Item {
        width: root.width - 2 * root.padding
        height: copy.height
        Txt {
            anchors.verticalCenter: parent.verticalCenter
            text: root.latest ? T.latestDetail(root.latest, Insights.today) : ""
            variant: "small"
            tone: "secondary"
        }
        Row {
            id: copy
            anchors.right: parent.right
            anchors.rightMargin: -Theme.space1
            spacing: Theme.space1
            Button {
                objectName: "latestHistory"
                text: T.text.history
                iconName: "history"
                size: "small"
                variant: "ghost"
                onClicked: Navigation.go("history")
            }
            CopyButton {
                text: T.text.copy
                copiedText: Words.copied
                size: "small"
                variant: "ghost"
                onCopied: Insights.copyLatest()
            }
        }
    }
}
