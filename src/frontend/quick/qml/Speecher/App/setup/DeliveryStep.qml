pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/text"

// Choose the delivery steps, each with what it needs on this computer. On
// Linux, keeping Press sets up the virtual keyboard that presses the keys;
// accessibility is the next step's to ask for.
// The steps are drafts, which finishing setup saves; one always stays on.
StepPage {
    id: page

    component StepChoice: SettingRow {
        id: choice
        required property string rowId
        readonly property var spec: Schema.row(rowId)
        readonly property var step: System.deliverySteps.find(s => s.id === spec.step) || {}
        readonly property bool on: !!page.choices[spec.step]
        readonly property bool last: on && ["insert", "press", "keep"].filter(id => page.choices[id]).length === 1

        title: spec.label
        iconName: spec.icon
        help: status.failure || [on ? spec.help : spec.offHelp || spec.help,
                                 last ? "It's the only step on, so it stays on." : ""].filter(s => s).join(" ")
        Row {
            spacing: 12
            NeedStatus {
                id: status
                need: choice.spec.need
                ready: !!choice.step.ready
                quiet: !choice.on
                // The Accessibility step after this one asks for it.
                fixable: choice.spec.need.check !== "desktopAccessibility"
                anchors.verticalCenter: parent.verticalCenter
            }
            Switch {
                checked: choice.on
                enabled: !choice.last
                anchors.verticalCenter: parent.verticalCenter
                onToggled: page.choices[choice.spec.step] = checked
            }
        }
    }

    Card {
        StepChoice { rowId: "insertThroughAccessibility" }
        StepChoice { rowId: "pressPasteKeys" }
        VirtualKeyboardTest {}
        StepChoice { rowId: "keepOnClipboard" }
    }
}
