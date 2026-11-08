pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "HistoryWords.js" as T

// One History entry on its day's card: its words, three lines of them until
// it opens, where and when, and how it ended where it didn't simply paste;
// Copy, Play and More on hover. A dictation opens in place on Return or a
// click, and EntryDetail grows under it. A file and a Recording open in the
// reader instead (readRequested): a file shows its name over two lines of
// its words, a Recording its name, and while a search runs the lines that
// match, each opening the reader there. The keys the page names act on the
// row with focus.
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
    readonly property bool recording: entry.kind === "recording"
    // Files and Recordings open in the reader; dictations in place.
    readonly property bool opensReader: recording || entry.kind === "file"
    readonly property alias rowItem: row
    signal toggled()
    // A Recording's reader, at the line at atMs, or its start for -1.
    signal readRequested(real atMs)
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
        Accessible.name: root.recording ? T.recordingTitle(root.entry) : root.opensReader ? T.fileName(root.entry) : root.entry.text
        Accessible.description: T.detail(root.entry)
        implicitHeight: content.implicitHeight + 22
        onClicked: root.opensReader ? root.readRequested(-1) : root.toggled()
        Keys.onUpPressed: root.stepFocus(-1)
        Keys.onDownPressed: root.stepFocus(1)
        Keys.onDeletePressed: root.deleteRequested()
        Keys.onPressed: event => {
            const command = event.modifiers & Qt.ControlModifier
            if (event.matches(StandardKey.Copy)) root.copy()
            else if (command && !root.opensReader && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) root.pasteAgain()
            else if (command && event.key === Qt.Key_Space) root.togglePlay()
            else return
            event.accepted = true
        }

        Column {
            id: content
            width: parent.width
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1
            // A file's or a Recording's name.
            Txt {
                visible: root.opensReader
                width: parent.width
                text: root.recording ? T.recordingTitle(root.entry) : T.fileName(root.entry)
                variant: "label"
                strong: true
                elide: Text.ElideRight
            }
            // The words, but a Recording's, which are its matching lines.
            Txt {
                visible: !root.recording
                width: parent.width
                text: root.open && root.spoken ? RichText.escape(root.whole.raw || "") : root.entry.html
                textFormat: Text.StyledText
                variant: "transcript"
                tone: root.opensReader ? "secondary" : "primary"
                wrapMode: Text.WordWrap
                maximumLineCount: root.open ? 2147483647 : root.opensReader ? 2 : 3
                elide: Text.ElideRight
            }
            Row {
                width: parent.width
                spacing: Theme.space2
                AppIcon {
                    visible: root.entry.kind !== "file" && !root.recording
                    name: root.entry.app
                    size: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                Icon {
                    visible: root.entry.kind === "file" || root.recording
                    name: root.recording ? "circle-dot" : "file-audio"
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
            // A Recording's lines that match the search, each a link to it.
            Repeater {
                model: root.recording ? root.entry.lines : []
                LinkedText {
                    required property var modelData
                    width: parent.width
                    plainText: T.lineClock(modelData.atMs) + " " + modelData.speaker + ": " + modelData.text
                    link: T.lineClock(modelData.atMs)
                    variant: "small"
                    onActivated: root.readRequested(modelData.atMs)
                }
            }
        }

        trailing: Row {
            spacing: Theme.space2
            StatusText {
                visible: root.entry.live
                text: T.text.recording
                tone: "live"
                anchors.verticalCenter: parent.verticalCenter
            }
            Txt {
                text: T.outcome(root.entry)
                variant: "small"
                tone: root.entry.outcome === "undelivered" || root.entry.outcome === "missed" ? "warning" : "secondary"
                anchors.verticalCenter: parent.verticalCenter
            }
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
                    items: [root.opensReader ? { id: "open", label: T.text.open, icon: "file-text" }
                                             : { id: "paste", label: T.text.pasteAgain, icon: "clipboard-paste" },
                            { id: "delete", label: T.text.delete_, icon: "trash-2" }]
                    onPicked: id => id === "paste" ? root.pasteAgain() : id === "open" ? root.readRequested(-1) : root.deleteRequested()
                }
            }
        ]
    }

    Reveal {
        open: root.open && !root.opensReader
        EntryDetail {
            entry: root.entry
            raw: root.open ? root.whole.raw || "" : ""
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
