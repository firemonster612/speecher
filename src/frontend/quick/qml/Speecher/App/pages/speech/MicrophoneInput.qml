import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The Microphone page's input card, which its setup step shows too: whether
// Speecher may use the microphone, which one it records, and a saved one
// that has gone.
Section {
    id: input
    readonly property var device: Schema.row("audioDevice")

    title: device.section
    // Home lists the same check, and opens it here.
    HealthRow { rowId: "microphoneAccess" }
    SchemaRow {
        rowId: "audioDevice"
        // The system default says which microphone it is now.
        note: !Settings.values.audioDevice && Audio.hasMicrophones
              ? spec.defaultLabel + ": " + spec.defaultHelp.arg(Microphones.deviceName) + "." : spec.help
        Select {
            placeholder: Words.choose
            enabled: Audio.hasMicrophones
            options: Microphones.deviceOptions
            currentId: Settings.values.audioDevice || ""
            onChosen: id => Settings.set("audioDevice", id)
        }
    }
    Reveal {
        open: Microphones.deviceMissing
        Notice {
            inset: Theme.rowInset
            text: input.device.missingHelp
            actionText: input.device.missingActionLabel
            onAction: Settings.set("audioDevice", "")
        }
    }
}
