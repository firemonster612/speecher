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
    readonly property bool unsupported: Health.noShortcuts(System.checks)
    subtitle: unsupported ? Setup.text.shortcutCommandOnly : info.description || ""
    Card {
        // Where the desktop gives no shortcut, the command runs one
        // dictation, so the shortcut and what pressing it does fold away.
        Reveal {
            open: !page.unsupported
            SchemaRow { rowId: "globalShortcut" }
            SchemaRow { rowId: "shortcutBehavior" }
        }
        // Where the desktop gives no shortcut, the command starts dictation
        // instead, and the step can go on without keys.
        Reveal {
            open: page.unsupported
            StartCommandRow {}
        }
        Reveal {
            open: !page.unsupported
            SchemaRow { rowId: "cancelShortcut"; note: Setup.text.cancelShortcutHelp }
        }
    }
}
