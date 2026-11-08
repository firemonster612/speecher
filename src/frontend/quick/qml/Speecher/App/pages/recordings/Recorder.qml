pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "RecordingWords.js" as T

// The recorder at the top of Recordings. Idle: a name, Record and what it
// will do, then each thing in its way with its own fix, whether or not
// setup has finished: the microphone, a speech service that can record,
// and setup itself, which Record waits for. The service's sign-in shows
// under Run with, as on Transcribe files. The first recording ever asks the
// consent question first. Running: the status line with the
// time and what it started with, a row per stream with its level and
// state, the problems core reports, and Cancel while it connects, then
// Stop, which reads Finishing… while the last lines are written. A start
// that failed says why under it.
Section {
    id: recorder
    objectName: "recorder"

    readonly property var t: T.text
    readonly property string phase: Recording.state
    readonly property bool idle: phase === "off"
    readonly property bool canRecord: Recording.refusal.length === 0
    // Why Record is off, the most specific reason first.
    readonly property string blocker: !Recording.plan.service ? Recording.refusal : Recording.setupPending ? t.setupFirst : ""
    // The microphone's access, then whether there is one, as Home says it.
    readonly property var access: Health.find(System.checks, "microphoneAccess")
    readonly property string microphoneProblem: !Health.passes(access) ? Health.status(access)
        : !Audio.hasMicrophones ? Schema.row("audioDevice").noneLabel
        : Microphones.deviceMissing ? Schema.row("audioDevice").missingLabel : ""
    property bool askingConsent: false
    // A start the page asked for, so a failure is shown where it was asked.
    property bool starting: false

    function record() {
        if (Recording.consentNeeded) {
            askingConsent = true
            return
        }
        Recording.start(name.text)
    }

    SearchLanding { rowIds: ["recordingControl"]; target: recorder; onLanded: if (recorder.idle) name.forceActiveFocus() }

    Switcher {
        width: parent.width
        travel: 0
        currentIndex: recorder.idle ? 0 : 1

        // Idle: the name, Record and what it will do.
        Column {
            Item {
                width: parent.width
                implicitHeight: record.implicitHeight + Theme.space4 * 2
                Txt {
                    id: nameLabel
                    x: Theme.rowInset
                    text: recorder.t.name
                    anchors.verticalCenter: parent.verticalCenter
                }
                TextField {
                    id: name
                    objectName: "recordingName"
                    anchors.left: nameLabel.right
                    anchors.leftMargin: Theme.space3
                    width: Math.min(Theme.fieldWidth, record.x - x - Theme.space3)
                    placeholderText: recorder.t.namePlaceholder
                    anchors.verticalCenter: parent.verticalCenter
                    onAccepted: if (recorder.canRecord) recorder.record()
                }
                Button {
                    id: record
                    objectName: "recordButton"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.rowInset
                    anchors.verticalCenter: parent.verticalCenter
                    text: recorder.t.record
                    variant: "primary"
                    enabled: recorder.canRecord && !recorder.askingConsent
                    tooltip: recorder.blocker
                    focus: true
                    trailing: StatusDot { color: Theme.negative; anchors.verticalCenter: parent.verticalCenter }
                    onClicked: recorder.record()
                }
            }
            Reveal {
                open: !!Recording.plan.service
                Txt {
                    x: Theme.rowInset
                    width: parent.width - Theme.rowInset * 2
                    bottomPadding: Theme.space4
                    text: T.summary(Recording.plan, Models.service(Recording.plan.service).label,
                                    StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", ""))
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                }
            }
            Reveal {
                open: recorder.microphoneProblem.length > 0
                Notice {
                    objectName: "recordingMicrophone"
                    text: recorder.microphoneProblem
                    inset: Theme.rowInset
                    actionText: recorder.t.openMicrophone
                    onAction: Navigation.follow("microphone", Health.passes(recorder.access) ? "audioDevice" : "microphoneAccess")
                }
            }
            Reveal {
                open: recorder.blocker.length > 0
                Notice {
                    objectName: "recordingRefusal"
                    text: recorder.blocker
                    inset: Theme.rowInset
                    actionText: Recording.plan.service ? recorder.t.runSetup : recorder.t.openSpeech
                    onAction: Recording.plan.service ? Settings.act("runSetup") : Navigation.follow("speech", "speechServices")
                }
            }
            InlineConfirm {
                open: recorder.askingConsent
                text: recorder.t.consentQuestion
                detail: recorder.t.consentDetail
                confirmText: recorder.t.record
                confirmIcon: "circle-dot"
                variant: "primary"
                cancelText: Words.cancel
                onCancelled: recorder.askingConsent = false
                onConfirmed: {
                    recorder.askingConsent = false
                    Recording.confirmConsent()
                    Recording.start(name.text)
                }
            }
        }

        // Running: what it records, how each stream is doing, and Stop.
        Column {
            Item {
                width: parent.width
                implicitHeight: stop.implicitHeight + Theme.space4 * 2
                Row {
                    x: Theme.rowInset
                    width: stop.x - x - Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space3
                    StatusText {
                        id: status
                        text: recorder.phase === "starting" ? recorder.t.connecting : recorder.t.recording
                        tone: recorder.phase === "recording" ? "live" : "neutral"
                        busy: recorder.phase === "starting"
                        variant: "label"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Txt {
                        text: Recording.title
                        strong: true
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, parent.width - status.width - clock.implicitWidth - Theme.space3 * 2)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MorphText {
                        id: clock
                        text: T.clock(Recording.elapsedMs)
                        variant: "mono"
                        tone: "secondary"
                        opacity: recorder.phase === "starting" ? 0 : 1
                        Behavior on opacity { Ease {} }
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Button {
                    id: stop
                    objectName: "stopRecording"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.rowInset
                    anchors.verticalCenter: parent.verticalCenter
                    // Cancel while it connects, which records nothing.
                    text: recorder.phase === "stopping" ? recorder.t.finishing
                        : recorder.phase === "starting" ? recorder.t.cancel : recorder.t.stop
                    iconName: recorder.phase === "starting" ? "x" : "square"
                    busy: recorder.phase === "stopping"
                    enabled: recorder.phase !== "stopping"
                    onClicked: Recording.stop()
                }
            }
            Txt {
                x: Theme.rowInset
                width: parent.width - Theme.rowInset * 2
                bottomPadding: Theme.space3
                visible: !!Recording.running.service
                text: T.runningSummary(Recording.running, Models.service(Recording.running.service || "").label)
                variant: "small"
                tone: "secondary"
                wrapMode: Text.WordWrap
            }
            StreamRows { width: parent.width }
            Reveal {
                open: Recording.echoWarning.length > 0
                Notice { text: Recording.echoWarning; inset: Theme.rowInset }
            }
            Reveal {
                open: Recording.unwrittenLines > 0
                Notice { text: T.writeProblem(Recording.unwrittenLines, Recording.writeError); tone: "negative"; inset: Theme.rowInset }
            }
        }
    }

    // Why the last start failed, until the next.
    Reveal {
        open: recorder.idle && Recording.problem.length > 0
        Column {
            width: parent.width
            Notice {
                objectName: "recordingProblem"
                text: Recording.problem
                tone: "negative"
                inset: Theme.rowInset
            }
        }
    }
}
