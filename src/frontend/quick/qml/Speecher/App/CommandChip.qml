import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// A command set in code type, with a button that copies it.
Rectangle {
    id: chip
    property string command
    width: cmd.implicitWidth + 44
    height: 30
    radius: Theme.radius
    color: Theme.sheet
    border.width: 1
    border.color: Theme.border
    Txt { id: cmd; x: 12; anchors.verticalCenter: parent.verticalCenter; text: chip.command; variant: "code" }
    CopyButton {
        copiedText: Words.copied
        tooltip: Words.copy
        size: "small"
        onCopied: System.copy(chip.command)
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
    }
}
