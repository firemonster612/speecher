import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/dictation"

// Dictation's shortcut and what pressing it does. Where the desktop gives
// no shortcut, the command that runs one dictation instead.
StepPage {
    id: page
    // The desktop gives Speecher no shortcut, so the command starts dictation.
    readonly property bool unsupported: Health.noShortcuts(System.checks)
    subtitle: unsupported ? Words.noShortcutHere : info.description || ""

    Reveal {
        open: !page.unsupported
        ShortcutHero {}
    }
    Reveal {
        open: !page.unsupported
        Section {
            title: Schema.row("shortcutBehavior").section
            SchemaRow { rowId: "shortcutBehavior" }
        }
    }
    Reveal {
        open: page.unsupported
        Section { StartCommandRow {} }
    }
}
