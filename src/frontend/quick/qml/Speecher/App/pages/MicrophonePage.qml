import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "speech"

Page {
    id: page
    title: Schema.page("microphone").title
    subtitle: Schema.page("microphone").subtitle

    readonly property var device: Schema.row("audioDevice")

    Section {
        id: testSection
        plain: true
        MicCheck {}
        SearchLanding { rowIds: ["microphoneTest"]; target: testSection }
    }

    Section {
        title: page.device.section
        SchemaRow {
            rowId: "audioDevice"
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
                text: page.device.missingHelp
                actionText: page.device.missingActionLabel
                onAction: Settings.set("audioDevice", "")
            }
        }
    }

    Section {
        title: Schema.row("captureMode").section
        SchemaRow { rowId: "captureMode" }
        SchemaRow { rowId: "readinessTimeoutMs" }
    }

    Section {
        title: Schema.row("preRollMs").section

        // The strip the sliders below draw on.
        Item {
            width: parent.width
            height: timeline.implicitHeight + Theme.space5 * 2
            RecordingTimeline {
                id: timeline
                x: Theme.rowInset
                y: Theme.space5
                width: parent.width - Theme.rowInset * 2
                beforeMs: Settings.values.preRollMs
                afterMs: Settings.values.postRollMs
                skipSilence: !!Settings.values.vadEnabled
                beforeLabel: Schema.valueLabel(Schema.row("preRollMs"), beforeMs)
                afterLabel: Schema.valueLabel(Schema.row("postRollMs"), afterMs)
            }
        }
        SchemaRow { rowId: "preRollMs" }
        SchemaRow { rowId: "postRollMs" }
        SchemaRow { rowId: "vadEnabled" }
        SchemaRow { rowId: "vadThresholdPercent" }
    }
}
