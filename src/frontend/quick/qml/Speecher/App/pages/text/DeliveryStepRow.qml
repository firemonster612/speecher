pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One delivery step: its switch, and before it what the step relies on
// here with its fix (NeedStatus), the first missing need or else its main
// one. A step that is on and waits for a need says so first in its note,
// with how to get a missing tool. The last step on can't be turned off, and
// says why. Items declared inside go between the fix and the switch. The
// switch saves the setting, or with draft only says switched, for the setup
// assistant's drafts, which then give steps (System.deliveryWith's).
SchemaRow {
    id: row

    property var steps: System.deliverySteps
    property bool draft: false
    signal switched(bool checked)
    readonly property var step: steps.find(s => s.id === spec.step) || {}
    readonly property bool on: !!step.on
    readonly property bool _last: on && steps.filter(s => s.on).length === 1
    readonly property var _waiting: step.waiting || []
    readonly property string _needId: _waiting[0] || (step.needs || [])[0] || ""
    readonly property var _need: Schema.pasting.needs[_needId] || {}
    // A need a step before this one shows already, with its fix.
    readonly property bool _shared: _needId !== "" && steps
        .slice(0, ["insert", "press", "keep"].indexOf(spec.step)).some(s => (s.needs || []).includes(_needId))
    default property alias extra: extras.data

    note: [on && _waiting.length ? Schema.pasting.waitingFor(_needId) + "." : "",
           on && _waiting.length ? _need.repair || "" : "",
           status.failure || (on ? spec.help : spec.offHelp || spec.help),
           Schema.pasting.details(spec.step, System.waylandSession),
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
