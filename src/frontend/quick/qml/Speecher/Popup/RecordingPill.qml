import QtQuick
import QtQuick.Effects
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The recording pill: while a recording runs and no dictation is up, a small
// pill at the popup's place says so, with its time and a round Stop.
// Connecting… while it starts, a warning dot while a stream needs attention,
// Finishing… while the last lines are written, then Saved with Open, which
// opens it in History, until the result's time is up. Clicking its words
// opens Recordings. It words itself (t).
Item {
    id: pill

    // The popup may show it: a recording runs (or just finished), the pill
    // setting is on, and no dictation is up.
    property bool allowed: false
    readonly property string phase: Recording.state
    // Saved, for the result's time after a recording stops.
    property bool saved: false
    readonly property bool shown: allowed && (phase !== "off" || saved)
    readonly property bool attention: Recording.streams.some(s => s.state === "reconnecting" || s.state === "stopped")
    // Faded out, so the window carrying it can go.
    readonly property bool empty: !shown && body.opacity === 0
    readonly property alias shadow: shadow

    readonly property var t: ({
        recording: "Recording",
        connecting: "Connecting…",
        finishing: "Finishing…",
        saved: "Saved",
        callAudioStopped: "Call audio stopped",
        stop: "Stop",
        open: "Open"
    })
    function clock(ms) {
        const s = Math.floor(ms / 1000)
        const mmss = Math.floor(s / 60) % 60 + ":" + String(s % 60).padStart(2, "0")
        return s < 3600 ? mmss : Math.floor(s / 3600) + ":" + mmss.padStart(5, "0")
    }

    property string _last: phase
    onPhaseChanged: {
        if (_last === "stopping" && phase === "off") {
            // A result time of 0 shows no result.
            saved = savedTime.interval > 0
            if (saved) savedTime.restart()
        } else if (phase !== "off") {
            saved = false
        }
        _last = phase
    }
    Timer {
        id: savedTime
        // The setting's real time, so not a Theme duration.
        interval: Settings.values.resultDuration ?? 2500
        onTriggered: pill.saved = false
    }

    implicitWidth: row.implicitWidth + 24
    implicitHeight: 40

    Item {
        id: body
        anchors.fill: parent
        opacity: pill.shown ? 1 : 0
        scale: pill.shown ? 1 : Theme.motionScale(0.9)
        transformOrigin: Item.Bottom
        Behavior on opacity { Ease { duration: Theme.base } }
        Behavior on scale { Ease { duration: pill.shown ? Theme.slow : Theme.base; curve: pill.shown ? Theme.springy : Theme.easeIn } }

        RectangularShadow {
            id: shadow
            anchors.fill: shell
            radius: shell.radius
            blur: 24
            spread: -4
            offset.y: 8
            color: Theme.shadow
        }
        Rectangle {
            id: shell
            anchors.fill: parent
            radius: height / 2
            color: Theme.sheet
            border.width: 1
            border.color: Theme.borderStrong
        }
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 10
            Item {
                width: 12
                height: 12
                anchors.verticalCenter: parent.verticalCenter
                Spinner { anchors.centerIn: parent; size: 12; opacity: pill.phase === "starting" || pill.phase === "stopping" ? 1 : 0 }
                StatusDot {
                    anchors.centerIn: parent
                    color: pill.attention ? Theme.warning : pill.saved ? Theme.positive : Theme.negative
                    pulse: pill.phase === "recording" && !pill.attention
                    opacity: pill.phase === "recording" || pill.saved ? 1 : 0
                }
            }
            MorphText {
                objectName: "recordingPillText"
                text: pill.saved ? pill.t.saved
                    : pill.phase === "starting" ? pill.t.connecting
                    : pill.phase === "stopping" ? pill.t.finishing
                    : pill.attention ? pill.t.callAudioStopped + " " + pill.clock(Recording.elapsedMs)
                    : pill.t.recording + " " + pill.clock(Recording.elapsedMs)
                variant: "label"
                strong: true
                anchors.verticalCenter: parent.verticalCenter
                TapHandler { onTapped: Navigation.openInMain("recordings") }
            }
            CircleButton {
                visible: !pill.saved
                size: 26
                iconName: "square"
                tooltip: pill.t.stop
                enabled: pill.phase === "recording"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: Recording.stop()
            }
            Button {
                visible: pill.saved
                text: pill.t.open
                size: "small"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: Navigation.openInMain("history", "recording:" + Recording.lastEntry)
            }
        }
    }
}
