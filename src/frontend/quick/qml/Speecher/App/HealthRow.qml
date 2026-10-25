import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// One permission or health check: its schema row, with the state worded by
// Health and the fix core offers for it. Starting a fix folds the button
// away while the state turns to a spinner, so only one thing says it is
// working; a fix that fails says why in place of the help. A page whose own
// row shows a check names it with checkId; items declared inside follow the
// fix button. In the setup assistant, fromSetup runs the fix as setup does.
// A check this computer doesn't have, such as the virtual keyboard off
// Linux, hides its row.
SchemaRow {
    id: row
    property string checkId: rowId
    property bool fromSetup: false
    default property alias extra: controls.data
    readonly property var check: Health.find(System.checks, checkId)
    shown: check.id !== undefined
    readonly property string _fix: Health.fix(check)
    readonly property bool _canFix: _fix.length > 0 && check.state !== "working"

    help: Health.failure(check) || Health.detail(check)

    Row {
        id: controls
        spacing: Theme.space3
        StateText {
            stateId: row.check.state
            text: Health.status(row.check)
            anchors.verticalCenter: parent.verticalCenter
        }
        // The button's room eases open and shut as it fades.
        Item {
            width: row._canFix ? fixButton.implicitWidth : 0
            height: fixButton.implicitHeight
            anchors.verticalCenter: parent.verticalCenter
            Behavior on width { Ease {} }
            Button {
                id: fixButton
                anchors.right: parent.right
                text: row._fix
                size: "small"
                variant: row.check.state === "off" ? "primary" : "secondary"
                enabled: row._canFix
                opacity: row._canFix ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease { duration: row._canFix ? Theme.base : Theme.fast } }
                onClicked: row.fromSetup ? System.fixFromSetup(row.checkId) : System.fix(row.checkId)
            }
        }
    }
}
