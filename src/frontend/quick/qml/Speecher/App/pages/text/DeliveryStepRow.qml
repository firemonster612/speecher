pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One delivery step: its switch, and before it what the step relies on
// here with its fix (NeedStatus), the first missing need or else its main
// one. A step that is on and waits for a need says so first in its note.
// The last step on can't be turned off, and says why. Items declared inside
// go between the fix and the switch. The switch saves the setting, or with
// draft only says switched, for the setup assistant's drafts, which then
// give on and stepsOn.
SchemaRow {
    id: row

    readonly property var step: System.deliverySteps.find(s => s.id === spec.step) || {}
    property bool on: !!step.on
    // The ids of the steps that are on.
    property var stepsOn: System.deliverySteps.filter(s => s.on).map(s => s.id)
    property bool draft: false
    signal switched(bool checked)
    readonly property bool _last: on && stepsOn.length === 1
    readonly property var _waiting: step.waiting || []
    readonly property string _needId: _waiting[0] || (step.needs || [])[0] || ""
    // A need a step before this one shows already, with its fix.
    readonly property bool _shared: _needId !== "" && System.deliverySteps
        .slice(0, ["insert", "press", "keep"].indexOf(spec.step)).some(s => (s.needs || []).includes(_needId))
    default property alias extra: extras.data

    note: [on && _waiting.length ? Schema.pasting.waitingFor(_needId) + "." : "",
           status.failure || (on ? spec.help : spec.offHelp || spec.help),
           Schema.pasting.details[spec.step] || "",
           _last ? "It's the only step on, so it stays on." : ""].filter(s => s).join(" ")

    Row {
        spacing: Theme.space3
        NeedStatus {
            id: status
            needId: row._needId
            there: !row._waiting.includes(row._needId)
            shared: row._shared
            // Keep off still needs saving the clipboard for Press.
            quiet: !row.on && row.spec.step !== "keep"
            anchors.verticalCenter: parent.verticalCenter
        }
        Row {
            id: extras
            spacing: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
        }
        Switch {
            checked: row.on
            enabled: !row._last
            anchors.verticalCenter: parent.verticalCenter
            onToggled: row.draft ? row.switched(checked) : Settings.set(row.rowId, checked)
        }
    }
}
