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
// card's. While a call records, a row says so first, with Stop and Open.
Section {
    id: session
    objectName: "homeSession"

    readonly property var t: T.text
    readonly property bool live: Dictation.state === "listening" || Dictation.state === "paused"
    readonly property var shortcutKeys: Settings.values.globalShortcut || []
    readonly property bool hasShortcut: shortcutKeys.length > 0
    // The desktop gives Speecher no shortcut, so there is none to set.
    readonly property bool noShortcuts: Health.noShortcuts(System.checks)
    // Every check that applies here, by id, in two groups: what dictation
    // can't start without (a check core marks needed) and what it works
    // without. The virtual keyboard is Pasting's, and so is accessibility
    // while a delivery step that is on relies on it; microphone access is
    // the Microphone row's.
    readonly property var steps: System.deliverySteps
    readonly property bool pastingNeedsAccessibility: steps.some(s => s.on && s.needs.includes("accessibility"))
    readonly property var checks: System.checks.filter(c => Health.applies(c) && c.id !== "virtualKeyboardHealth"
                                                             && c.id !== "microphoneAccess"
                                                             && (c.id !== "desktopAccessibility" || !pastingNeedsAccessibility))
    readonly property var neededChecks: checks.filter(c => c.needed).map(c => c.id)
    readonly property var optionalChecks: checks.filter(c => !c.needed).map(c => c.id)

    // The speech services in order: dictation works while any of them does. Checking while none works and one is being checked;
    // a service nobody has checked yet isn't ready, though dictation can
    // start to try it, and one whose check failed is missing.
    readonly property var chain: ModelText.order("speech").map(id => ModelText.service(id))
    readonly property var working: chain.find(s => s.state === "ready")
    readonly property string speechState: working ? "ok"
        : chain.some(s => s.signInChecking) ? "checking"
        : chain.some(s => s.state === "unknown" && !s.signInCheckFailure) ? "notChecked" : "attention"
    readonly property var speechItem: ({
        id: "speech", title: Schema.page("speech").title, state: speechState, page: "speech", row: "",
        status: working ? (working === chain[0] ? ModelText.status(working) : t.takesOver(working.label))
              : speechState === "checking" ? ModelText.checking
              : ModelText.status(chain.find(s => s.state === "unknown") || chain[0])
    })
    // The microphone, one row: access first, with its fix, then whether
    // the one chosen is there.
    readonly property var access: Health.find(System.checks, "microphoneAccess")
    readonly property bool accessFails: !Health.passes(access)
    readonly property var microphoneItem: ({
        id: "microphone", title: Schema.page("microphone").title, page: "microphone", row: accessFails ? "microphoneAccess" : "audioDevice",
        state: accessFails ? access.state : !Audio.hasMicrophones || Microphones.deviceMissing ? "missing" : "ok",
        status: accessFails ? Health.status(access)
              : !Audio.hasMicrophones ? Schema.row("audioDevice").noneLabel
              : Microphones.deviceMissing ? Schema.row("audioDevice").missingLabel : Microphones.deviceName,
        help: accessFails ? Health.failure(access) || Health.detail(access) : "",
        fixLabel: accessFails ? Health.fix(access) : "",
        fixCheck: "microphoneAccess"
    })
    // Pasting, one row while a step that is on waits for something: where
    // the words end up, what is missing by name, and the first fix.
    readonly property var waiting: [...new Set([].concat(...steps.filter(s => s.on).map(s => s.waiting)))]
    readonly property var fixFor: waiting.map(n => Schema.pasting.needs[n])
        .find(need => need.check && Health.fix(Health.find(System.checks, need.check))) || null
    readonly property var pastingItem: ({
        id: "pasting", title: Schema.page("pasting").title, page: "pasting", row: "",
        state: System.pastingOutcome === "nothing" ? "off" : "attention",
        status: Schema.pasting.outcomes[System.pastingOutcome],
        help: [t.needs(waiting.map(n => Schema.pasting.needs[n].label))]
            .concat(waiting.map(n => Schema.pasting.needs[n].repair || "")).filter(s => s).join(" "),
        fixLabel: fixFor ? fixFor.fix || Health.fix(Health.find(System.checks, fixFor.check)) : "",
        fixCheck: fixFor ? fixFor.check : ""
    })
    readonly property bool pastingDegraded: waiting.length > 0 || System.pastingOutcome !== "delivers"
    // With nothing to deliver the words, dictation is for nothing.
    readonly property bool pastingNeeded: System.pastingOutcome === "nothing"

    // What dictation needs and doesn't have, as the caption names it, from
    // the same results as the rows.
    readonly property var missing: checks.filter(c => c.needed && !Health.passes(c)).map(c => Schema.row(c.id).label.toLowerCase())
        .concat(accessFails ? [Schema.row("microphoneAccess").label.toLowerCase()] : [],
                !Audio.hasMicrophones ? [t.aMicrophone] : Microphones.deviceMissing ? [t.yourMicrophone] : [],
                speechState === "attention" ? [t.aSpeechService] : [],
                pastingNeeded ? [t.aWayToDeliver] : [])
    // Without microphone access or a microphone nothing can start; with no
    // working speech service it can, to try again, but isn't ready.
    readonly property bool cantStart: accessFails || checks.some(c => c.needed && !Health.passes(c)) || !Audio.hasMicrophones
    readonly property bool blocked: missing.length > 0
    readonly property bool allPass: !blocked && checks.every(Health.passes) && microphoneItem.state === "ok"
                                    && speechState === "ok" && !pastingDegraded
    // What the dictation is doing, as the heading says it.
    readonly property var status: ({ starting: "Listening", listening: "Listening", paused: "Paused",
                                     transcribing: "Transcribing", refining: "Refining", error: "Dictation failed" })

    title: status[Dictation.state] || (blocked ? t.notReady : speechState === "checking" ? t.checking
                                       : speechState === "notChecked" ? t.notChecked : t.ready)
    // What the listed things do to dictation, in words, else how to start.
    caption: T.readiness(missing) || (speechState === "notChecked" ? t.tryIt : "")
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
                Button { text: session.t.setShortcut; size: "small"; onClicked: Navigation.follow("dictation", "globalShortcut") }
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

    Reveal {
        open: Recording.state !== "off"
        ListRow {
            objectName: "homeRecording"
            iconName: "circle-dot"
            title: T.recordingLine(Recording.elapsedMs)
            detail: Recording.title
            trailing: Row {
                spacing: Theme.space2
                Button {
                    text: session.t.stopRecording
                    iconName: "square"
                    size: "small"
                    enabled: Recording.state === "recording"
                    onClicked: Recording.stop()
                }
                Button { text: session.t.openRecording; size: "small"; onClicked: Navigation.follow("recordings", "recordingControl") }
            }
        }
    }

    Switcher {
        travel: 0
        currentIndex: session.live ? 1 : 0

        // How ready Speecher is.
        Readiness {
            allPass: session.allPass
            neededChecks: session.neededChecks
            optionalChecks: session.optionalChecks
            neededItems: [session.microphoneItem, session.speechItem].concat(session.pastingDegraded && session.pastingNeeded ? [session.pastingItem] : [])
            optionalItems: session.pastingDegraded && !session.pastingNeeded ? [session.pastingItem] : []
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
