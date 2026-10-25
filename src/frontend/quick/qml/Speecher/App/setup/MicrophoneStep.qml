import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// Allow the microphone, pick one, then talk: a wide meter moves with the
// voice and a line under it, marked with the live-sound red once speech
// comes through, confirms Speecher hears it.
StepPage {
    id: page

    // The microphone test runs while the step is on show.
    Component.onCompleted: if (Audio.testState === "stopped") Audio.toggleTest()
    Component.onDestruction: if (Audio.testState !== "stopped") Audio.toggleTest()

    Card {
        HealthRow { rowId: "microphoneAccess" }
        SchemaRow {
            rowId: "audioDevice"
            Select {
                placeholder: Words.choose
                options: Audio.deviceOptions
                currentId: Settings.values.audioDevice || ""
                onChosen: id => Settings.set("audioDevice", id)
            }
        }
    }

    Plate {
        height: 150
        Waveform {
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.space6
            height: 64
            bars: 56
            barWidth: 4
            gap: 3
            level: Audio.level
        }
        StatusText {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space5
            text: Audio.heard ? Audio.heardText : Audio.testStatus
            tone: Audio.heard ? "live" : "tertiary"
        }
    }

    Txt {
        width: parent.width
        text: Audio.setupHint
        variant: "small"
        tone: "secondary"
        wrapMode: Text.WordWrap
    }
}
