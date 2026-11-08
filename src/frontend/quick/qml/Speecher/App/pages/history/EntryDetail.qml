pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "HistoryWords.js" as T

// An opened History entry's actions, under its row: Refined or Spoken where
// they differ, then Copy, Paste again, Play with where it is, Refine again
// and Delete. Refine again offers the Writing Profiles, the entry's own
// first, and the new text shows under the actions with Keep this and Copy.
Item {
    id: root
    required property var entry
    // The entry's Raw Transcript, which Refine again needs.
    property string raw
    property bool spoken: false
    property bool playing: false
    // Refine again's text, and whether it is on its way.
    property string refinedText
    property bool refining: false
    property bool refineFailed: false
    signal spokenPicked(bool spoken)
    signal copyRequested()
    signal pasteRequested()
    signal playRequested()
    signal deleteRequested()

    width: parent ? parent.width : implicitWidth
    implicitHeight: column.implicitHeight + Theme.space3

    Connections {
        target: History
        function onRefined(id, text) {
            if (id !== root.entry.id) return
            root.refining = false
            root.refinedText = text
        }
        function onRefineFailed(id, message) {
            if (id !== root.entry.id) return
            root.refining = false
            root.refineFailed = true
        }
    }

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
                text: root.playing ? T.clock(History.playing.positionMs || 0) : T.clock(root.entry.durationMs)
                size: "small"
                onClicked: root.playRequested()
            }
            MenuButton {
                objectName: "refineAgain"
                visible: root.raw.length > 0
                label: root.refining ? T.text.refining : T.text.refineAgain
                size: "small"
                enabled: !root.refining
                items: {
                    const own = Library.profiles.filter(p => p.id === root.entry.profile)
                    return own.concat(Library.profiles.filter(p => p.id !== root.entry.profile)).map(p => ({ id: p.id, label: p.name }))
                }
                onPicked: id => {
                    root.refinedText = ""
                    root.refineFailed = false
                    root.refining = true
                    History.refineAgain(root.entry.id, id)
                }
            }
            Button { text: T.text.delete_; iconName: "trash-2"; size: "small"; variant: "danger"; onClicked: root.deleteRequested() }
        }
        Reveal {
            open: root.refinedText.length > 0
            Column {
                width: parent.width
                spacing: Theme.space2
                Txt { width: parent.width; text: root.refinedText; variant: "transcript"; wrapMode: Text.WordWrap }
                Row {
                    spacing: Theme.space2
                    Button {
                        text: T.text.keepThis
                        iconName: "check"
                        size: "small"
                        variant: "primary"
                        onClicked: if (History.keepRefined(root.entry.id, root.refinedText)) root.refinedText = ""
                    }
                    Button { text: T.text.copy; iconName: "copy"; size: "small"; onClicked: System.copy(root.refinedText) }
                }
            }
        }
        Reveal {
            open: root.refineFailed
            Notice { text: T.text.refineFailed; tone: "negative" }
        }
    }
}
