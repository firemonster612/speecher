pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "words.js" as T

// The dictation itself, at the top of Home: whether Speecher is ready and
// its shortcut. While dictating, the level meter and the session's buttons
// take the shortcut's place and the words heard so far take the readiness
// card's.
Section {
    id: session
    objectName: "homeSession"

    readonly property var t: T.text
    readonly property bool live: Dictation.state === "listening" || Dictation.state === "paused"
    readonly property var shortcutKeys: Settings.values.globalShortcut || []
    readonly property bool hasShortcut: shortcutKeys.length > 0
    // The desktop gives Speecher no shortcut, so there is none to set.
    readonly property bool noShortcuts: Health.noShortcuts(System.checks)
    // What the dictation is doing, as the heading says it.
    readonly property var status: ({ starting: "Listening", listening: "Listening", paused: "Paused",
                                     transcribing: "Transcribing", refining: "Refining", error: "Dictation failed" })

    title: status[Dictation.state] || (readiness.blocked ? t.notReady : readiness.speechState === "checking" ? t.checking
                                       : readiness.speechState === "notChecked" ? t.notChecked : t.ready)
    // What the listed things do to dictation, in words, else how to start.
    caption: T.readiness(readiness.missing) || (readiness.speechState === "notChecked" ? t.tryIt : "")
           || (hasShortcut ? t.pressShortcut : noShortcuts ? t.noShortcutHere : t.noShortcut)
    actions: Switcher {
        id: controls
        width: implicitWidth
        travel: 0
        currentIndex: session.live ? 1 : 0

        // Start dictation, which needs no shortcut, after Set shortcut while
        // there is a shortcut to set. While nothing can start, the card
        // below says why instead.
        Row {
            spacing: Theme.space2
            KeyCombo { keys: session.shortcutKeys; anchors.verticalCenter: parent.verticalCenter }
            Item { width: Theme.space3; height: 1; visible: session.hasShortcut }
            Reveal {
                sideways: true
                open: !readiness.cantStart && !session.hasShortcut && !session.noShortcuts
                anchors.verticalCenter: parent.verticalCenter
                Button { text: session.t.setShortcut; size: "small"; onClicked: Navigation.follow("dictation", "globalShortcut") }
            }
            Reveal {
                sideways: true
                open: !readiness.cantStart
                anchors.verticalCenter: parent.verticalCenter
                Button {
                    text: session.t.startDictation
                    iconName: "mic"
                    size: "small"
                    onClicked: Dictation.startRequested()
                }
            }
        }
        Row {
            spacing: Theme.space2
            Waveform {
                bars: 24
                height: 22
                level: Dictation.level
                active: Dictation.state === "listening"
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: Theme.space1; height: 1 }
            Button { text: session.t.stop; iconName: "square"; size: "small"; variant: "primary"; onClicked: Dictation.stopRequested() }
            Button {
                text: Dictation.state === "paused" ? Words.resume : Words.pause
                size: "small"
                onClicked: Dictation.pauseRequested()
            }
            Button { text: Words.cancel; size: "small"; variant: "ghost"; onClicked: Dictation.cancelRequested() }
        }
    }

    Switcher {
        travel: 0
        currentIndex: session.live ? 1 : 0

        // How ready Speecher is.
        Readiness { id: readiness }
        // What is being heard now.
        Txt {
            padding: Theme.space5
            topPadding: Theme.space4
            text: Dictation.words
            variant: "lead"
            wrapMode: Text.WordWrap
        }
    }
}
