pragma Singleton
import QtQuick
import Speecher.Backend
import Speecher.Wording

// The microphones as the UI words them, from Audio's devices and the saved
// Input device: the device choices, whether the saved one has gone, and what
// the microphone test says. The microphone page and setup both read these.
QtObject {
    readonly property var _row: Schema.row("audioDevice")
    readonly property var _test: Schema.row("microphoneTest")
    readonly property string _chosen: Settings.values.audioDevice || ""
    // What the system default records from: the platform's default, or the
    // first microphone when it names none, as recording does.
    readonly property var _default: Audio.devices.find(d => d.isDefault) || Audio.devices[0]

    // The Input device's choices: the system default first, then each
    // microphone; a saved microphone that has gone stays, marked missing, so
    // the choice is not silently lost.
    readonly property var deviceOptions: {
        const missing = { id: _chosen, label: _row.missingLabel, icon: "mic-off", help: _row.missingHelp }
        if (!Audio.hasMicrophones) {
            const none = [{ id: "", label: _row.noneLabel, icon: "mic-off", help: _row.noneHelp }]
            return _chosen ? none.concat([missing]) : none
        }
        const options = [{ id: "", label: _row.defaultLabel, help: _row.defaultHelp.arg(_default.label) }]
            .concat(Audio.devices.map(d => ({ id: d.id, label: d.label })))
        return options.some(o => o.id === _chosen) ? options : options.concat([missing])
    }
    readonly property bool deviceMissing: _chosen !== "" && Audio.hasMicrophones
                                          && !Audio.devices.some(d => d.id === _chosen)
    // The microphone the test and dictation record from.
    readonly property string deviceName: {
        const device = Audio.devices.find(d => d.id === _chosen) || _default
        return device ? device.label : ""
    }

    // What the test says: the microphone it listens to, or why it can't.
    readonly property string testStatus: System.check("microphoneAccess").state !== "ok" ? _test.accessOffText
                                       : !Audio.hasMicrophones ? _test.noMicrophoneText
                                       : Audio.testError !== "" ? Audio.testError
                                       : _test.listeningText.arg(deviceName)
}
