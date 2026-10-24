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

    // The microphone test runs while the step is on show. When its device
    // goes mid-test, it moves to the one that records now; when one won't
    // open, a microphone plugged in or chosen tries again.
    function listen() { if (Audio.testState === "stopped") Audio.toggleTest() }
    Component.onCompleted: listen()
    Component.onDestruction: Audio.stopTest()
    Connections {
        target: Audio
        function onDevicesChanged() { page.listen() }
    }
    readonly property string device: Settings.values.audioDevice || ""
    onDeviceChanged: listen()
    // The reason for a failure arrives just after the stop.
    readonly property string testState: Audio.testState
    property bool _running: false
    onTestStateChanged: {
        if (_running && testState === "stopped") {
            Qt.callLater(() => { if (Audio.testError !== "") page.listen() })
        }
        _running = testState === "running"
    }

    readonly property var test: Schema.row("microphoneTest")

    Card {
        HealthRow { rowId: "microphoneAccess" }
        SchemaRow {
            rowId: "audioDevice"
            Select {
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
        }
        StatusText {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space5
            text: Audio.heard ? page.test.heardText : Microphones.testStatus
            tone: Audio.heard ? "live" : Audio.testError !== "" ? "negative" : "tertiary"
        }
    }

    Txt {
        width: parent.width
        text: page.test.setupHint
        variant: "small"
        tone: "secondary"
        wrapMode: Text.WordWrap
    }
}
