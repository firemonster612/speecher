import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The startCommand row: the command that starts one dictation, to run in a
// terminal or bind to a key, with a button that copies it.
SchemaRow {
    id: command
    rowId: "startCommand"
    Rectangle {
        width: cmd.implicitWidth + 44
        height: 30
        radius: Theme.radius
        color: Theme.sheet
        border.width: 1
        border.color: Theme.border
        Txt { id: cmd; x: 12; anchors.verticalCenter: parent.verticalCenter; text: command.spec.command; variant: "code" }
        CopyButton {
            copiedText: Words.copied
            tooltip: Words.copy
            size: "small"
            onCopied: System.copy(command.spec.command)
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
