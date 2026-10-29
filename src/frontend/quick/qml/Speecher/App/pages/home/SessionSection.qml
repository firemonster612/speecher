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
    // What Speecher knows stands in the way of dictating, each with the
    // page that fixes it: microphone access, a microphone, the speech
    // service. A service not checked yet stands in nobody's way. Without
    // a microphone nothing can start (blocked); the chosen service only
    // warns, since a fallback or a Writing Profile's own service may do.
    readonly property var service: ModelText.service(Settings.values.speechProvider || "")
    readonly property var access: Health.find(System.checks, "microphoneAccess")
    // Where words can neither be inserted (no accessibility) nor pasted
    // with keys (no virtual keyboard), they only reach the clipboard;
    // unless that is what was chosen, Home says so without blocking.
    readonly property bool clipboardOnly: System.wordsStayCopied
    readonly property var blockers: [
        access.state === "off" ? { id: "access", title: Schema.row("microphoneAccess").label, status: Health.status(access), page: "permissions" } : null,
        Audio.hasMicrophones ? null : { id: "microphone", title: Schema.page("microphone").title, status: Schema.row("audioDevice").noneLabel, page: "microphone" },
        service.state !== "ready" && service.state !== "unknown" ? { id: "speech", title: Schema.page("speech").title, status: ModelText.status(service), page: "speech" } : null,
        clipboardOnly ? { id: "pasting", title: Schema.page("pasting").title, status: t.wordsOnClipboard, page: "pasting" } : null
    ].filter(b => b)
    readonly property bool blocked: blockers.some(b => b.id !== "speech" && b.id !== "pasting")
    // What the dictation is doing, as the heading says it.
    readonly property var status: ({ starting: "Listening", listening: "Listening", paused: "Paused",
                                     transcribing: "Transcribing", refining: "Refining", error: "Dictation failed" })

    title: status[Dictation.state] || (blocked ? t.notReady : t.ready)
    // What the listed things do to dictation, in words, else how to start.
    caption: T.readiness(blockers.map(b => b.id))
           || (hasShortcut ? t.pressShortcut : noShortcuts ? t.noShortcutHere : t.noShortcut)
    actions: Switcher {
        id: controls
        width: implicitWidth
        travel: 0
        currentIndex: session.live ? 1 : 0

        // Start dictation, which needs no shortcut, after Set shortcut while
        // there is a shortcut to set. While something stands in the way,
        // the card below lists it instead.
        Row {
            spacing: Theme.space2
            KeyCombo { keys: session.shortcutKeys; anchors.verticalCenter: parent.verticalCenter }
            Item { width: Theme.space3; height: 1; visible: session.hasShortcut }
            Reveal {
                sideways: true
                open: !session.blocked && !session.hasShortcut && !session.noShortcuts
                anchors.verticalCenter: parent.verticalCenter
                Button { text: session.t.setShortcut; size: "small"; onClicked: Navigation.go("shortcuts") }
            }
            Reveal {
                sideways: true
                open: !session.blocked
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

    // What stands in the way, each row opening the page that fixes it.
    component BlockerRows: RowList {
        id: blockerRows
        keyOf: b => b.id
        delegate: SettingRow {
            id: blockerRow
            objectName: "blocker_" + key
            required property string key
            required property int index
            readonly property var blocker: blockerRows.entry(key)
            first: index === 0
            title: blockerRow.blocker.title
            clickable: true
            onClicked: Navigation.go(blockerRow.blocker.page)
            StatusText { text: blockerRow.blocker.status; tone: "warning" }
        }
    }

    Switcher {
        travel: 0
        currentIndex: session.live ? 1 : session.blocked ? 2 : 0

        // What only limits dictation, if anything, over the latest dictation,
        // where it went, and Copy.
        Column {
            width: parent.width
            BlockerRows { values: session.blocked ? [] : session.blockers }
            Reveal {
                open: !session.blocked && session.blockers.length > 0
                Rectangle { x: Theme.rowInset; width: parent.width - 2 * Theme.rowInset; height: 1; color: Theme.rule }
            }
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
        // Something stands in the way: only its rows.
        BlockerRows { values: session.blockers }
    }
}
