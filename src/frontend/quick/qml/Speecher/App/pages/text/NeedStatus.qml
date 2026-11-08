import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What a delivery step relies on here, by its need id (Schema.pasting.needs),
// as "<need>: <state>" and the fix its System check offers; a step that
// needs nothing shows nothing. quiet words the state in plain ink, for a
// step that is off and so needs nothing yet. shared is for a need an
// earlier step shows with its fix: missing, it says the step needs the same
// one, and offers no second button. statusWidth sets the status right in a
// space that wide, so a column of them lines their fixes up.
Row {
    id: root

    property string needId
    property bool there: false
    property bool quiet: false
    property bool shared: false
    property real statusWidth: 0
    readonly property real naturalStatusWidth: status.implicitWidth
    readonly property var need: Schema.pasting.needs[needId] || null
    readonly property var check: need && need.check ? Health.find(System.checks, need.check) : ({})
    readonly property string _fix: !shared && check.fix && check.state !== "working"
                                   ? need.fix || Health.fix(check) : ""

    visible: !!need
    spacing: Theme.space3
    Item {
        width: Math.max(root.statusWidth, status.implicitWidth)
        height: status.implicitHeight
        anchors.verticalCenter: parent.verticalCenter
        StateText {
            id: status
            anchors.right: parent.right
            stateId: root.check.state ?? (root.there ? "ok" : "attention")
            tone: root.quiet ? "neutral" : States.tone(stateId)
            text: !root.need ? ""
                : root.shared && !root.there ? Schema.pasting.sameNeed(root.needId)
                : root.need.label + ": " + ((root.there ? root.need.ready : root.need.notReady) || Health.status(root.check))
        }
    }
    Button {
        visible: root._fix.length > 0
        text: root._fix
        size: "small"
        anchors.verticalCenter: parent.verticalCenter
        onClicked: System.fix(root.need.check)
    }
}
