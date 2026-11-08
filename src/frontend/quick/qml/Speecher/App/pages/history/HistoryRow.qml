pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "HistoryWords.js" as T

// One History entry on its day's card: its words, three lines of them until
// it opens, where and when, and how it ended where it didn't simply paste;
// Copy, Play and More on hover. Return or a click opens it in place, and
// EntryDetail grows under it. The keys the page names act on the row with
// focus.
Column {
    id: root
    // One of History.results.
    required property var entry
    property bool first: false
    property bool open: false
    // The Spoken version shows in place of the Refined one.
    property bool spoken: false
    // The whole entry, read once it opens.
    readonly property var whole: open ? History.entry(entry.id) : ({})
    readonly property bool playing: History.playing.id === entry.id
    readonly property alias rowItem: row
    signal toggled()
    signal deleteRequested()
    signal stepFocus(int step)

    function copy() { History.copy(entry.id, spoken); Toaster.show(T.text.copied, { icon: "copy" }) }
    function pasteAgain() { History.pasteAgain(entry.id, spoken) }
    function togglePlay() {
        if (!entry.hasAudio) return
        if (playing) History.stopPlaying()
        else History.play(entry.id)
    }

    width: parent ? parent.width : implicitWidth
    onOpenChanged: if (!open) spoken = false

    ListRow {
        id: row
        objectName: "historyRow_" + root.entry.id
        first: root.first
        clickable: true
        selected: root.open
        Accessible.role: Accessible.ListItem
        Accessible.name: root.entry.text
        Accessible.description: T.detail(root.entry)
        implicitHeight: content.implicitHeight + 22
        onClicked: root.toggled()
        Keys.onUpPressed: root.stepFocus(-1)
        Keys.onDownPressed: root.stepFocus(1)
        Keys.onDeletePressed: root.deleteRequested()
        Keys.onPressed: event => {
            const command = event.modifiers & Qt.ControlModifier
            if (event.matches(StandardKey.Copy)) root.copy()
            else if (command && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) root.pasteAgain()
            else if (command && event.key === Qt.Key_Space) root.togglePlay()
            else return
            event.accepted = true
        }

        Column {
            id: content
            width: parent.width
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            Txt {
                width: parent.width
                text: root.open && root.spoken ? RichText.escape(root.whole.raw || "") : root.entry.html
                textFormat: Text.StyledText
                variant: "transcript"
                wrapMode: Text.WordWrap
                maximumLineCount: root.open ? 2147483647 : 3
                elide: Text.ElideRight
            }
            Row {
                width: parent.width
                spacing: Theme.space2
                AppIcon {
                    visible: root.entry.kind !== "file"
                    name: root.entry.app
                    size: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                Icon {
                    visible: root.entry.kind === "file"
                    name: "file-audio"
                    size: 16
                    color: Theme.textSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }
                Txt {
                    width: parent.width - 24
                    text: T.detail(root.entry)
                    variant: "small"
                    tone: "secondary"
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        trailing: Txt {
            text: T.outcome(root.entry)
            variant: "small"
            tone: root.entry.outcome === "undelivered" ? "warning" : "secondary"
        }
        actions: [
            CopyButton {
                tooltip: T.text.copy
                copiedText: T.text.copied
                onCopied: History.copy(root.entry.id, root.spoken)
            },
            IconButton {
                visible: root.entry.hasAudio
                iconName: root.playing ? "square" : "play"
                size: 28
                tooltip: root.playing ? T.text.stop : T.text.play
                onClicked: root.togglePlay()
            },
            IconButton {
                iconName: "ellipsis"
                size: 28
                tooltip: T.text.more
                onClicked: more.open()
                Menu {
                    id: more
                    width: 200
                    items: [
                        { id: "paste", label: T.text.pasteAgain, icon: "clipboard-paste" },
                        { id: "delete", label: T.text.delete_, icon: "trash-2" }
                    ]
                    onPicked: id => id === "paste" ? root.pasteAgain() : root.deleteRequested()
                }
            }
        ]
    }

    Reveal {
        open: root.open
        EntryDetail {
            entry: root.entry
            spoken: root.spoken
            playing: root.playing
            onSpokenPicked: spoken => root.spoken = spoken
            onCopyRequested: root.copy()
            onPasteRequested: root.pasteAgain()
            onPlayRequested: root.togglePlay()
            onDeleteRequested: root.deleteRequested()
        }
    }
}
