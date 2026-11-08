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
// will do, or why it can't; the first recording ever asks the consent
// question first. Running: the status line with the time, a row per stream
// with its level and state, the problems core reports, and Stop, which
// reads Finishing… while the last lines are written. A start that failed
// says why under it.
Section {
    id: recorder
    objectName: "recorder"

    readonly property var t: T.text
    readonly property string phase: Recording.state
    readonly property bool idle: phase === "off"
    readonly property bool canRecord: Recording.refusal.length === 0
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
                    tooltip: Recording.refusal
                    focus: true
                    trailing: StatusDot { color: Theme.negative; anchors.verticalCenter: parent.verticalCenter }
                    onClicked: recorder.record()
                }
            }
            Reveal {
                open: recorder.canRecord
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
                open: !recorder.canRecord
                Notice {
                    objectName: "recordingRefusal"
                    text: Recording.refusal
                    inset: Theme.rowInset
                    // Nothing in the Service Order can record; a start that
                    // waits for setup says so without one.
                    actionText: Recording.plan.service ? "" : recorder.t.openSpeech
                    onAction: Navigation.follow("speech", "speechServices")
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
                    text: recorder.phase === "stopping" ? recorder.t.finishing : recorder.t.stop
                    iconName: "square"
                    busy: recorder.phase === "stopping"
                    enabled: recorder.phase === "recording"
                    onClicked: Recording.stop()
                }
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
