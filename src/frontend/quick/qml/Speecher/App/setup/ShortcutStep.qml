import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// The same shortcut rows as the Shortcuts page: what starts dictation, what
// pressing it does, and an optional key to throw a dictation away.
StepPage {
    Card {
        SchemaRow { rowId: "globalShortcut" }
        SchemaRow { rowId: "shortcutBehavior" }
        SchemaRow { rowId: "cancelShortcut"; help: System.setupText.cancelShortcutHelp }
    }
}
