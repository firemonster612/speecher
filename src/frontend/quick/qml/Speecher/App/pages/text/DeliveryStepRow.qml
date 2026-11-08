pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One delivery step: its switch, and before it each thing the step relies
// on here, one status line apiece (NeedStatus). A need an earlier step
// shows too keeps its fix there: here it shows its state while it is there,
// and missing says it needs the same one. A step that is on and waits for needs
// names them all first in its note, with how to get a missing tool. The
// last step on can't be turned off, and says why. Items declared inside go
// between the needs and the switch. An App's or App Type's own steps show
// the same way through ownSteps.
SchemaRow {
    id: row

    // An App's or App Type's own steps ({ insert, press, keep }), shown in
    // place of the Pasting page's; a flip then asks stepToggled instead of
    // saving the setting.
    property var ownSteps: null
    // The paste keys' id those steps press (Schema.pasting.keyChoices),
    // which Press's help names.
    property string keys: "standard"
    signal stepToggled(bool on)

    readonly property var steps: ownSteps ? System.stepsPlan(ownSteps).steps : System.deliverySteps
    readonly property var step: steps.find(s => s.id === spec.step) || {}
    readonly property bool on: !!step.on
    readonly property bool _last: on && steps.filter(s => s.on).length === 1
    readonly property var _waiting: step.waiting || []
    // The needs a step before this one has too, which it shows with the fix.
    readonly property var _earlier: [].concat(...steps.slice(0, Schema.pasting.stepIds.indexOf(spec.step)).map(s => s.needs || []))
    readonly property var _shown: step.needs || []
    // Why one of the needs' last fix failed, or "".
    readonly property string _failure: _shown.filter(n => !_earlier.includes(n)).map(n => Schema.pasting.needs[n].check)
        .filter(c => c).map(c => Health.failure(Health.find(System.checks, c))).find(f => f) || ""
    default property alias extra: extras.data

    note: [on && _waiting.length ? Schema.pasting.waitingFor(_waiting) + "." : "",
           on ? _waiting.map(n => Schema.pasting.needs[n].repair || "").filter(s => s).join(" ") : "",
           _failure || (on ? spec.help : spec.offHelp || spec.help)
               .replace(Schema.pasting.pasteKeys + " when", Schema.pasting.keysLabel(keys) + " when"),
           Schema.pasting.details(spec.step, System.waylandSession),
           _last ? "It's the only step on, so it stays on." : ""].filter(s => s).join(" ")

    Row {
        spacing: Theme.space3
        Column {
            id: needs
            spacing: Theme.space1
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: row._shown
                NeedStatus {
                    required property string modelData
                    anchors.right: parent ? parent.right : undefined
                    needId: modelData
                    there: !row._waiting.includes(modelData)
                    shared: row._earlier.includes(modelData)
                    // Keep off still needs saving the clipboard for Press.
                    quiet: !row.on && row.spec.step !== "keep"
                }
            }
        }
        Row {
            id: extras
            spacing: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
        }
        Switch {
            objectName: "stepSwitch_" + row.spec.step
            checked: row.on
            enabled: !row._last
            anchors.verticalCenter: parent.verticalCenter
            onToggled: row.ownSteps ? row.stepToggled(checked) : Settings.set(row.rowId, checked)
        }
    }
}
