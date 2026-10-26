import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Allow the microphone, pick one, then talk: a wide meter moves with the
// voice and a line under it, marked with the live-sound red once speech
// comes through, confirms Speecher hears it.
StepPage {
    id: page

    // The microphone test runs while the step is on show.
    Component.onCompleted: Audio.holdTest(page)
    Component.onDestruction: Audio.releaseTest(page)

    readonly property var test: Schema.row("microphoneTest")

    Card {
        HealthRow { rowId: "microphoneAccess" }
        SchemaRow {
            rowId: "audioDevice"
            Select {
                enabled: Audio.hasMicrophones
                placeholder: Words.choose
                options: Microphones.deviceOptions
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
            // Silent bars in the rule's colour, so red means sound is heard.
            compress: true
        }
        StatusText {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space5
            // With no microphone the select already says so, so the plate
            // says what to do in its place.
            text: !Audio.hasMicrophones ? page.test.noMicrophoneHint
                : Audio.heard ? page.test.heardText
                : Audio.testState === "running" || Audio.testError !== "" ? Microphones.testStatus
                : page.test.notHeardText
            tone: !Audio.hasMicrophones ? "neutral" : Audio.heard ? "live" : Audio.testError !== "" ? "negative" : "tertiary"
        }
    }

    // What to do with a microphone: speak. Without one, the plate says it.
    Reveal {
        open: Audio.hasMicrophones
        Txt {
            width: parent.width
            text: page.test.setupHint
            variant: "small"
            tone: "secondary"
            wrapMode: Text.WordWrap
        }
    }
}
