pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One delivery step: its switch, and before it what the step needs on this
// computer with its fix (NeedStatus). The last step on can't be turned off,
// and says why. Items declared inside go between the fix and the switch.
SchemaRow {
    id: row

    readonly property var step: System.deliverySteps.find(s => s.id === spec.step) || {}
    readonly property bool on: !!step.on
    readonly property bool _last: on && System.deliverySteps.filter(s => s.on).length === 1
    default property alias extra: extras.data

    note: [status.failure || (on ? spec.help : spec.offHelp || spec.help),
           (spec.need || {}).detail || "",
           _last ? "It's the only step on, so it stays on." : ""].filter(s => s).join(" ")

    Row {
        spacing: Theme.space3
        NeedStatus {
            id: status
            need: row.spec.need || {}
            ready: !!row.step.ready
            quiet: !row.on
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
            onToggled: Settings.set(row.rowId, checked)
        }
    }
}
