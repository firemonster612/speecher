pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/home"

// The finish: a check that lands once, how to dictate with the chosen
// shortcut or, where the desktop gives none, the command, and Home's
// readiness card, the same checks over setup's draft. While a step still
// needs attention there is no check and the title says setup is not done.
// The rail already lists every step's outcome.
StepPage {
    id: page
    title: ""

    readonly property var _open: page.choices.steps.filter(s => !s.done && s.id !== "ready")
    readonly property var _needed: _open.filter(s => s.needed)
    // The step that sets what a readiness row opens, by page.
    readonly property var _stepOf: ({ speech: "transcription", services: "transcription", microphone: "microphone",
                                      pasting: "delivery", refinement: "refinement", dictation: "shortcut" })
    readonly property var _keys: Settings.values.globalShortcut || []
    readonly property bool _command: Health.noShortcuts(System.checks)

    Column {
        spacing: Theme.space3
        // Finishing setup is a rare moment, so its check springs in.
        Reveal {
            open: page._open.length === 0
            Rectangle {
                width: 52
                height: 52
                radius: width / 2
                color: Theme.accent
                scale: Theme.motionScale(0.4)
                Component.onCompleted: scale = 1
                Behavior on scale { Ease { duration: Theme.page; curve: Theme.springy } }
                Icon { anchors.centerIn: parent; name: "check"; size: 28; color: Theme.accentText }
            }
        }
        MorphText { text: page._needed.length ? Setup.text.notReady : page._open.length ? Setup.text.almostReady : page.info.title; variant: "display" }
        // The shortcut, and what pressing it does as the Shortcuts page set it.
        Reveal {
            open: page._keys.length > 0 && !page._command
            Row {
                spacing: Theme.space2
                KeyCombo { keys: page._keys; anchors.verticalCenter: parent.verticalCenter }
                Txt { text: Schema.valueHelp(Schema.row("shortcutBehavior"), Settings.values.shortcutBehavior); tone: "secondary"; anchors.verticalCenter: parent.verticalCenter }
            }
        }
        // The command that starts dictation where the desktop gives no shortcut.
        Reveal {
            open: page._command
            Column {
                spacing: Theme.space2
                Txt { text: Setup.text.startWithCommand; tone: "secondary" }
                CommandChip { objectName: "readyCommand"; command: Schema.row("startCommand").command }
            }
        }
    }

    // Home's readiness card, over the draft: a row with a step of its own
    // goes back to that step, any other opens its page beside setup.
    Card {
        width: parent.width
        Readiness {
            inSetup: true
            openRow: (pageId, rowId) => {
                const step = page._stepOf[pageId]
                if (step) page.choices.goTo(step)
                else Navigation.follow(pageId, rowId, "setup")
            }
        }
    }
}
