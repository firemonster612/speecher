pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.App

// A row of commands to bind beside the start command where the desktop
// gives Speecher no shortcut: its schema row's help, then each of its
// commands (spec.commands) with a copy button, on Dictation and setup's
// Shortcut step alike. Items declared inside follow the commands.
SchemaRow {
    id: row
    default property alias extra: commands.data
    stacked: true
    Flow {
        id: commands
        width: parent.width
        spacing: Theme.space2
        Repeater {
            model: row.spec.commands
            CommandChip { required property string modelData; command: modelData }
        }
    }
}
