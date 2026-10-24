pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme
import Speecher.Demo

// Mock of the audio input (host/QuickAudio in the app): the microphones the
// platform sees and the microphone test. --mic-state=ok|missing|none|silent
// picks the devices (silent: present, but the test hears nothing);
// --mic-test=running starts the test; --mic-test-error=<message> makes it
// fail to start; --vad=on turns Skip silence on.
QtObject {
    id: audio

    readonly property string _micState: Launch.arg("mic-state", "ok")

    // Microphones present now: [{ id, label, isDefault }].
    property var devices: _micState === "none" ? [] : [
        { id: "alsa_input.usb-Blue_Yeti", label: "Blue Yeti", isDefault: true },
        { id: "alsa_input.pci-0000_c1_00.6.analog-stereo", label: "Built-in microphone", isDefault: false },
        { id: "bluez_input.WH-1000XM5", label: "WH-1000XM5", isDefault: false }
    ]
    readonly property bool hasMicrophones: devices.length > 0

    function refreshDevices() {}

    // The microphone test: stopped, starting or running, and the live level
    // (0 to 1, linear amplitude) while it runs.
    property string testState: Launch.arg("mic-test", "stopped")
    readonly property bool canToggleTest: testState !== "starting"
    readonly property real level: _voice.level
    property string testError: ""

    // Whether the test has heard input since it last started.
    property bool heard: false
    onLevelChanged: if (level > 0.01) heard = true

    function toggleTest() {
        if (testState === "stopped") {
            heard = false
            testError = ""
            testState = "starting"
            _starting.restart()
        } else {
            testState = "stopped"
        }
    }
    function stopTest() { testState = "stopped" }

    readonly property string _device: Settings.values.audioDevice || ""
    // A running test moves to a newly chosen microphone and listens afresh.
    on_DeviceChanged: if (testState === "running") heard = false

    property Timer _starting: Timer {
        interval: Theme.ms(650)
        onTriggered: {
            const error = Launch.arg("mic-test-error", "")
            audio.testState = error ? "stopped" : "running"
            audio.testError = error
        }
    }

    // No sound arrives from a silent microphone, with none at all, or
    // without microphone access.
    readonly property DemoVoice _voice: DemoVoice {
        running: audio.testState === "running" && audio.hasMicrophones && audio._micState !== "silent"
                 && System.check("microphoneAccess").state === "ok"
    }

    Component.onCompleted: Qt.callLater(() => {
        if (_micState === "missing") Settings.set("audioDevice", "alsa_input.usb-RODE_NT-USB")
        if (Launch.arg("vad", "") === "on") Settings.set("vadEnabled", true)
    })
}
