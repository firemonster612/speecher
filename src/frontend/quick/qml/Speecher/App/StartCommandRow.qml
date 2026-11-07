import QtQuick
import Speecher.Backend
import Speecher.App

// The startCommand row: the command that starts one dictation, to run in a
// terminal or bind to a key, with a button that copies it. Where the desktop
// gives Speecher no shortcut, its help also says where this desktop binds a
// command to a key.
SchemaRow {
    id: command
    rowId: "startCommand"
    readonly property var _bind: spec.bindHelp || {}
    note: Health.noShortcuts(System.checks) ? spec.help + " " + (_bind[System.desktop] || _bind.other) : spec.help
    CommandChip { command: command.spec.command }
}
