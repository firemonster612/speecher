pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "HistoryWords.js" as T

// Refine again for one History entry: start(profile) refines its Raw
// Transcript again, and this shows the new text with Keep this and Copy, or
// why it couldn't. The menu that starts it lists profiles, the entry's own
// first. A dictation's EntryDetail and a file's EntryReader both use it.
Column {
    id: root

    required property var entry
    property string refinedText
    property bool refining: false
    property bool failed: false
    property string problem
    // Another entry in the same reader starts with nothing refined; the
    // last one's late answer is for its id, so it isn't taken.
    readonly property var _entryId: entry ? entry.id : 0
    on_EntryIdChanged: {
        refinedText = ""
        refining = false
        failed = false
        problem = ""
    }
    readonly property var profiles: {
        const own = Library.profiles.filter(p => p.id === root.entry.profile)
        return own.concat(Library.profiles.filter(p => p.id !== root.entry.profile)).map(p => ({ id: p.id, label: p.name }))
    }

    function start(profile) {
        refinedText = ""
        failed = false
        problem = ""
        refining = true
        History.refineAgain(entry.id, profile)
    }

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space3

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
            root.failed = true
            root.problem = message
        }
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
        open: root.failed
        Notice { text: root.problem || T.text.refineFailed; tone: "negative" }
    }
}
