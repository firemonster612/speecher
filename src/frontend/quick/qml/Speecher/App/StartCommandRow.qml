import QtQuick

// The startCommand row: the command that starts one dictation, to run in a
// terminal or bind to a key, with a button that copies it.
SchemaRow {
    id: command
    rowId: "startCommand"
    CommandChip { command: command.spec.command }
}
