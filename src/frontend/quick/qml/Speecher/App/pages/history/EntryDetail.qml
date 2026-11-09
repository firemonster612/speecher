pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "HistoryWords.js" as T

// An opened dictation's actions, under its row: Refined or As spoken where
// they differ, then Copy, Paste again, Play with where it is, Refine again
// and Delete. Files and Recordings open in EntryReader instead.
Item {
    id: root
    required property var entry
    // The entry's Raw Transcript, which Refine again needs.
    property string raw
    property bool spoken: false
    property bool playing: false
    signal spokenPicked(bool spoken)
    signal copyRequested()
    signal pasteRequested()
    signal playRequested()
    signal deleteRequested()

    width: parent ? parent.width : implicitWidth
    implicitHeight: column.implicitHeight + Theme.space3

    Column {
        id: column
        x: Theme.rowInset
        width: parent.width - 2 * Theme.rowInset
        spacing: Theme.space3
        Flow {
            width: parent.width
            spacing: Theme.space2
            SegmentedControl {
                visible: root.entry.hasRaw
                options: [{ id: "refined", label: T.text.refined }, { id: "spoken", label: T.text.spoken }]
                currentId: root.spoken ? "spoken" : "refined"
                onActivated: id => root.spokenPicked(id === "spoken")
            }
            Button { text: T.text.copy; iconName: "copy"; size: "small"; onClicked: root.copyRequested() }
            Button { text: T.text.pasteAgain; iconName: "clipboard-paste"; size: "small"; onClicked: root.pasteRequested() }
            Button {
                visible: root.entry.hasAudio
                iconName: root.playing ? "square" : "play"
                text: root.playing ? Words.clock(History.playing.positionMs || 0) : Words.clock(root.entry.durationMs)
                size: "small"
                onClicked: root.playRequested()
            }
            MenuButton {
                objectName: "refineAgain"
                visible: root.raw.length > 0
                label: refine.refining ? T.text.refining : T.text.refineAgain
                size: "small"
                enabled: !refine.refining
                items: refine.profiles
                onPicked: id => refine.start(id)
            }
            Button {
                text: T.text.delete_
                iconName: "trash-2"
                size: "small"
                variant: "danger"
                onClicked: root.deleteRequested()
            }
        }
        RefineAgain { id: refine; entry: root.entry }
    }
}
