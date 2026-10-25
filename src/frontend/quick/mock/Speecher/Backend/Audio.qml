pragma Singleton
import QtQuick
import Speecher.Wording
import Speecher.Launch
import Speecher.Theme
import Speecher.Demo

// Placeholder: the app ships this mock until its wiring PR replaces it.
// Mock of the audio input: the microphones core sees and the microphone test.
// --mic-state=ok|missing|none|silent picks the devices (silent: present, but
// the test hears nothing); --mic-test=running starts the test; --vad=on
// turns Skip silence on.
QtObject {
    id: audio

    readonly property string _micState: Launch.arg("mic-state", "ok")

    // Microphones present now: [{ id, label, help? }].
    property var devices: _micState === "none" ? [] : [
        { id: "alsa_input.usb-Blue_Yeti", label: "Blue Yeti", help: "USB microphone" },
        { id: "alsa_input.pci-0000_c1_00.6.analog-stereo", label: "Built-in microphone", help: "Laptop" },
        { id: "bluez_input.WH-1000XM5", label: "WH-1000XM5", help: "Bluetooth headset" }
    ]
    readonly property bool hasMicrophones: devices.length > 0

    // The input device choice, as core's audioDeviceOptions builds it: the
    // system default first, and a saved microphone that has gone stays as
    // "Missing microphone" so the choice is not silently lost.
    readonly property var deviceOptions: {
        const chosen = Settings.values.audioDevice || ""
        const missing = { id: chosen, label: "Missing microphone", icon: "mic-off",
                          help: "This saved microphone is not currently available." }
        if (!hasMicrophones) {
            const none = [{ id: "", label: "No microphones found", icon: "mic-off",
                            help: "Connect or enable an input device, then try again." }]
            return chosen ? none.concat([missing]) : none
        }
        const options = [{ id: "", label: "System default", help: devices[0].label + " right now" }]
            .concat(devices.map(d => ({ id: d.id, label: d.label, help: d.help })))
        return options.some(o => o.id === chosen) ? options : options.concat([missing])
    }
    readonly property bool deviceMissing: hasMicrophones && !devices.some(d => d.id === (Settings.values.audioDevice || ""))
                                          && (Settings.values.audioDevice || "") !== ""
    readonly property string deviceName: {
        const id = Settings.values.audioDevice || ""
        const d = devices.find(x => x.id === id)
        return d ? d.label : hasMicrophones ? devices[0].label : ""
    }

    function refreshDevices() {}
    function openSystemSettings() { System.open("your system's microphone settings") }

    // The microphone test: stopped, starting or running, and the live level
    // (0 to 1, linear amplitude) while it runs.
    property string testState: Launch.arg("mic-test", "stopped")
    readonly property real level: _voice.level
    readonly property string testCaption: testState === "running" ? "Stop test"
                                        : testState === "starting" ? "Starting…" : "Start test"
    // What the test says while it runs: the microphone it listens to, or
    // why it can't hear one. The schema row words it at rest.
    readonly property string testStatus: System.check("microphoneAccess").state !== "ok" ? "Microphone access is off."
                                       : !hasMicrophones ? "No microphone was found."
                                       : "Listening to " + deviceName

    // The Skip silence mark on the level: "Quiet below 2%".
    readonly property string thresholdText: Schema.row("vadThresholdPercent").label + " "
        + Schema.valueLabel(Schema.row("vadThresholdPercent"), Settings.values.vadThresholdPercent)

    // Whether the test has heard speech since it last started or the
    // microphone changed: the level went past what a quiet room gives.
    property bool heard: false
    readonly property real _speechLevel: 0.08
    onLevelChanged: if (level > _speechLevel) heard = true
    // Speech heard from one microphone says nothing about another.
    readonly property string _device: Settings.values.audioDevice || ""
    on_DeviceChanged: heard = false
    // What the test says once it has heard speech, and before.
    readonly property string heardText: "Speecher hears your microphone."
    readonly property string notHeardText: "Not heard yet"
    // Under the setup step's meter: what to do if it stays flat.
    readonly property string setupHint: "Say a few words. If the bars stay flat, check that the microphone isn't muted, or pick another one."

    function toggleTest() {
        if (testState === "stopped") {
            heard = false
            testState = "starting"
            _starting.restart()
        } else {
            testState = "stopped"
        }
    }

    property Timer _starting: Timer {
        interval: Theme.ms(650)
        onTriggered: audio.testState = "running"
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
