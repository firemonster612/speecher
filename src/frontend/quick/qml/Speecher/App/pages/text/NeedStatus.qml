import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// What a delivery step needs on this computer (a schema row's need), as a
// status and the fix its System check offers. A need with no check needs
// nothing of the person and says so. quiet words the state in plain ink, for
// a step that is off and so needs nothing yet. Without fixable it only says
// the state, for a place where another one offers the fix.
Row {
    id: root

    property var need: ({})
    property bool ready: false
    property bool quiet: false
    property bool fixable: true
    readonly property var check: need.check ? Health.find(System.checks, need.check) : ({})
    // Why the check's last fix failed, or "".
    readonly property string failure: Health.failure(check)
    readonly property string _fix: fixable && check.fix && check.state !== "working" ? need.fix || Health.fix(check) : ""

    spacing: Theme.space3
    StateText {
        stateId: root.check.state ?? (root.ready ? "ok" : "attention")
        tone: root.quiet ? "neutral" : States.tone(stateId)
        text: (root.ready ? root.need.ready : root.need.notReady) || Health.status(root.check)
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
