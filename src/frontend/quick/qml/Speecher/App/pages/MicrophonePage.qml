import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "speech"

// The microphone: whether Speecher may use it, which one, and a test, then
// the timing few people change, folded away until opened or searched for.
Page {
    id: page
    title: Schema.page("microphone").title
    subtitle: Schema.page("microphone").subtitle

    // The rows in the Advanced group, which opens for a search that lands
    // on one of them.
    readonly property var advancedRows: Schema.rows.filter(r => r.page === "microphone" && r.section === Schema.row("captureMode").section)
                                                   .map(r => r.id)
    Connections {
        target: Navigation
        function onPendingRowChanged() { if (page.advancedRows.includes(Navigation.pendingRow)) advanced.open = true }
    }

    Section {
        id: testSection
        plain: true
        MicCheck {}
        SearchLanding { rowIds: ["microphoneTest"]; target: testSection }
    }

    MicrophoneInput {}

    Section {
        title: Schema.row("captureMode").section
        Disclosure {
            id: advanced
            title: "Audio timing and silence"
            help: "Keeping the microphone open, how long to wait for it, and how much audio each dictation keeps."
            Component.onCompleted: open = page.advancedRows.includes(Navigation.pendingRow)

            SchemaRow { rowId: "captureMode" }
            SchemaRow { rowId: "readinessTimeoutMs" }
            // The strip the sliders below draw on.
            Item {
                width: parent.width
                height: timeline.implicitHeight + Theme.space5 * 2
                Rectangle { x: Theme.rowInset; width: parent.width - 2 * Theme.rowInset; height: 1; color: Theme.rule }
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
}
