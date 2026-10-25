import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "words.js" as T

// The dictation itself, at the top of Home: whether Speecher is ready and
// its shortcut, then the latest thing said, set large. While dictating, the
// level meter and the session's buttons take the shortcut's place and the
// words heard so far take the latest dictation's.
Section {
    id: session

    readonly property var t: T.text
    readonly property bool live: Dictation.state === "listening" || Dictation.state === "paused"
    readonly property var latest: Insights.latest
    readonly property bool hasShortcut: Status.shortcutKeys.length > 0
    // What the dictation is doing, as the heading says it.
    readonly property var status: ({ starting: "Listening", listening: "Listening", paused: "Paused",
                                     transcribing: "Transcribing", refining: "Refining", error: "Dictation failed" })

    title: status[Dictation.state] || "Ready to dictate"
    caption: hasShortcut ? t.pressShortcut : t.noShortcut
    actions: Switcher {
        id: controls
        width: implicitWidth
        travel: 0
        currentIndex: session.live ? 1 : 0

        Row {
            spacing: Theme.space5
            KeyCombo { keys: Status.shortcutKeys; anchors.verticalCenter: parent.verticalCenter }
            Button {
                text: session.hasShortcut ? session.t.startDictation : session.t.setShortcut
                iconName: session.hasShortcut ? "mic" : ""
                size: "small"
                onClicked: session.hasShortcut ? Dictation.startRequested() : Navigation.go("shortcuts")
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

        // The latest dictation, where it went, and Copy.
        Column {
            padding: Theme.space5
            topPadding: Theme.space4
            bottomPadding: Theme.space3
            spacing: Theme.space2
            Txt {
                width: parent.width - 2 * parent.padding
                text: session.latest ? session.latest.text : session.t.nothingYet
                tone: session.latest ? "primary" : "tertiary"
                variant: "lead"
                wrapMode: Text.WordWrap
            }
            Reveal {
                open: session.latest !== null
                width: parent.width - 2 * parent.padding
                Item {
                    width: parent.width
                    height: copy.height
                    Txt {
                        anchors.verticalCenter: parent.verticalCenter
                        text: session.latest ? T.latestDetail(session.latest, Insights.today) : ""
                        variant: "small"
                        tone: "secondary"
                    }
                    CopyButton {
                        id: copy
                        anchors.right: parent.right
                        anchors.rightMargin: -Theme.space1
                        text: session.t.copy
                        copiedText: Words.copied
                        size: "small"
                        variant: "ghost"
                        onCopied: Insights.copyLatest()
                    }
                }
            }
        }
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
