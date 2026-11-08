pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "RecordingWords.js" as T

// A row per stream of the recording running, the microphone first: who it
// is, its level, and its state in words. A stream that needs attention
// (reconnecting, stopped) gets the warning dot; nothing else does.
Column {
    id: rows

    // The two streams' rows stay made, opening as their stream runs, so a
    // row's level meter is never torn down while it moves.
    Repeater {
        model: 2
        Reveal {
            id: stream
            required property int index
            readonly property var modelData: Recording.streams[index] || ({ speaker: "", state: "", lostAudioMs: 0 })
            readonly property bool attention: modelData.state === "reconnecting" || modelData.state === "stopped"
            width: rows.width
            open: index < Recording.streams.length
            Item {
                width: parent.width
                implicitHeight: 36
                Row {
                    x: Theme.rowInset
                    width: parent.width - Theme.rowInset * 2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space3
                    Txt {
                        width: 48
                        text: T.speakers[stream.modelData.speaker] || stream.modelData.speaker
                        tone: "secondary"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Waveform {
                        width: 120
                        height: 20
                        bars: 22
                        level: Recording.levels[stream.modelData.speaker] || 0
                        active: stream.modelData.state === "recording"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    StatusText {
                        text: T.streamState(stream.modelData)
                        tone: stream.attention ? "warning" : stream.modelData.state === "paused" ? "tertiary" : "neutral"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
