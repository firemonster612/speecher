pragma ComponentBehavior: Bound
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
    objectName: "homeSession"

    readonly property var t: T.text
    readonly property bool live: Dictation.state === "listening" || Dictation.state === "paused"
    readonly property var latest: Insights.latest
    readonly property var shortcutKeys: Settings.values.globalShortcut || []
    readonly property bool hasShortcut: shortcutKeys.length > 0
    // The desktop gives Speecher no shortcut, so there is none to set.
    readonly property bool noShortcuts: Health.noShortcuts(System.checks)
    // Every check that applies here, by id, in two groups: what dictation
    // can't start without (a check core marks needed, such as microphone
    // access) and what it works without.
    readonly property var checks: System.checks.filter(Health.applies)
    readonly property var neededChecks: checks.filter(c => c.needed).map(c => c.id)
    readonly property var optionalChecks: checks.filter(c => !c.needed).map(c => c.id)

    // The speech chain, the service then its fallbacks: dictation works while
    // any of them does. Checking while none works and one is being checked;
    // a service nobody has checked yet stands in nobody's way.
    readonly property var chain: [Settings.values.speechProvider || ""]
        .concat(Settings.values.speechFallbackList || []).map(id => ModelText.service(id))
    readonly property var working: chain.find(s => s.state === "ready")
    readonly property string speechState: working ? "ok"
        : chain.some(s => s.signInChecking) ? "checking"
        : chain.some(s => s.state === "unknown") ? "notChecked" : "attention"
    readonly property var speechItem: ({
        id: "speech", title: Schema.page("speech").title, state: speechState, page: "speech", row: "",
        status: working ? (working === chain[0] ? ModelText.status(working) : t.takesOver(working.label))
              : speechState === "checking" ? ModelText.checking
              : ModelText.status(chain.find(s => s.state === "unknown") || chain[0])
    })
    readonly property var microphoneItem: ({
        id: "microphone", title: Schema.page("microphone").title, page: "microphone", row: "audioDevice",
        state: !Audio.hasMicrophones || Microphones.deviceMissing ? "missing" : "ok",
        status: !Audio.hasMicrophones ? Schema.row("audioDevice").noneLabel
              : Microphones.deviceMissing ? Schema.row("audioDevice").missingLabel : Microphones.deviceName
    })
    // Where words can neither be inserted nor pasted with keys, they only
    // reach the clipboard.
    readonly property var optionalItems: System.wordsStayCopied
        ? [{ id: "pasting", title: Schema.page("pasting").title, status: t.wordsOnClipboard, state: "attention", page: "pasting", row: "" }] : []

    // What dictation needs and doesn't have, as the caption names it.
    readonly property var missing: checks.filter(c => c.needed && !Health.passes(c)).map(c => Schema.row(c.id).label.toLowerCase())
        .concat(Audio.hasMicrophones ? [] : [t.aMicrophone], speechState === "attention" ? [t.aSpeechService] : [])
    // Without microphone access or a microphone nothing can start; with no
    // working speech service it can, to try again, but isn't ready.
    readonly property bool cantStart: checks.some(c => c.needed && !Health.passes(c)) || !Audio.hasMicrophones
    readonly property bool blocked: missing.length > 0
    readonly property bool allPass: !blocked && checks.every(Health.passes) && microphoneItem.state === "ok"
                                    && speechState === "ok" && optionalItems.length === 0
    // What the dictation is doing, as the heading says it.
    readonly property var status: ({ starting: "Listening", listening: "Listening", paused: "Paused",
                                     transcribing: "Transcribing", refining: "Refining", error: "Dictation failed" })

    title: status[Dictation.state] || (blocked ? t.notReady : speechState === "checking" ? t.checking : t.ready)
    // What the listed things do to dictation, in words, else how to start.
    caption: T.readiness(missing)
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
                open: !session.cantStart && !session.hasShortcut && !session.noShortcuts
                anchors.verticalCenter: parent.verticalCenter
                Button { text: session.t.setShortcut; size: "small"; onClicked: Navigation.goToRow("dictation", "globalShortcut") }
            }
            Reveal {
                sideways: true
                open: !session.cantStart
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

        // How ready Speecher is, over the latest dictation, where it went,
        // and Copy.
        Column {
            width: parent.width
            Readiness {
                allPass: session.allPass
                neededChecks: session.neededChecks
                optionalChecks: session.optionalChecks
                neededItems: [session.microphoneItem, session.speechItem]
                optionalItems: session.optionalItems
            }
            Rectangle { x: Theme.rowInset; width: parent.width - 2 * Theme.rowInset; height: 1; color: Theme.rule }
            Column {
                width: parent.width
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
