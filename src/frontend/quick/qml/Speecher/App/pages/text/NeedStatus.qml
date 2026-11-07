import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What a delivery step relies on here, by its need id (Schema.pasting.needs),
// as "<need>: <state>" and the fix its System check offers; no need says
// the step is built in. quiet words the state in plain ink, for a step that
// is off and so needs nothing yet. shared is for a need an earlier step
// shows with its fix: missing, it says the step needs the same one, and
// offers no second button. Without fixable it only says the state.
Row {
    id: root

    property string needId
    property bool there: false
    property bool quiet: false
    property bool shared: false
    property bool fixable: true
    readonly property var need: Schema.pasting.needs[needId] || null
    readonly property var check: need && need.check ? Health.find(System.checks, need.check) : ({})
    // Why the check's last fix failed, or "".
    readonly property string failure: Health.failure(check)
    readonly property string _fix: fixable && !shared && check.fix && check.state !== "working"
                                   ? need.fix || Health.fix(check) : ""

    spacing: Theme.space3
    StateText {
        stateId: root.check.state ?? (root.there || !root.need ? "ok" : "attention")
        tone: root.quiet || !root.need ? "neutral" : States.tone(stateId)
        text: !root.need ? Schema.pasting.builtIn
            : root.shared && !root.there ? Schema.pasting.sameNeed(root.needId)
            : root.need.label + ": " + ((root.there ? root.need.ready : root.need.notReady) || Health.status(root.check))
        anchors.verticalCenter: parent.verticalCenter
    }
    Button {
        visible: root._fix.length > 0
        text: root._fix
        size: "small"
        anchors.verticalCenter: parent.verticalCenter
        onClicked: System.fix(root.need.check)
    }
}
