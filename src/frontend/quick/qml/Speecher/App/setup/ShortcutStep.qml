import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The same shortcut rows as the Shortcuts page: what starts dictation, what
// pressing it does, and an optional key to throw a dictation away.
StepPage {
    id: page
    // The desktop gives Speecher no shortcut, so the command starts dictation.
    readonly property bool unsupported: Health.find(System.checks, "globalShortcuts").reason === "unsupported"
    subtitle: unsupported ? Setup.text.shortcutCommandOnly : info.description || ""
    Card {
        SchemaRow {
            rowId: "globalShortcut"
            // Where the desktop gives Speecher no shortcut, the field is
            // off and the row says so; the command starts dictation instead.
            enabled: _saves && !page.unsupported
            note: page.unsupported ? Words.noShortcutsHere : spec.help
        }
        SchemaRow { rowId: "shortcutBehavior" }
        SchemaRow { rowId: "cancelShortcut"; note: Setup.text.cancelShortcutHelp }
        // Where the desktop gives no shortcut, the command starts dictation
        // instead, and the step can go on without keys.
        Reveal {
            open: page.unsupported
            StartCommandRow {}
        }
    }
}
