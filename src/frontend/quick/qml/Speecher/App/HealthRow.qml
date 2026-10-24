import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// One permission or health check: its schema row, with the state worded by
// Health and the fix core offers for it. Starting a fix folds the button
// away while the state turns to a spinner, so only one thing says it is
// working. A page whose own row shows a check names it with checkId; items
// declared inside follow the fix button.
SchemaRow {
    id: row
    property string checkId: rowId
    default property alias extra: controls.data
    readonly property var check: System.check(checkId)
    readonly property string _fix: Health.fix(check)
    readonly property bool _canFix: _fix.length > 0 && check.state !== "working"

    help: Health.detail(check)

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
                onClicked: System.fix(row.checkId)
            }
        }
    }
}
