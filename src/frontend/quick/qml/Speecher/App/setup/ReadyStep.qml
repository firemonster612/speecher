pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The finish: a check that lands once, how to dictate with the chosen
// shortcut or, where the desktop gives none, the command, and a way back to
// any step that still needs attention. While a step does, there is no check
// and the title says setup is not done. The rail already lists every step's
// outcome. The groups and the title follow the same results as Home: a
// working speech service, the microphone and somewhere for the words to go
// are needed, the rest makes dictation better.
StepPage {
    id: page
    title: ""

    readonly property var _open: page.choices.steps.filter(s => !s.done && s.id !== "ready")
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

    Txt {
        text: page._open.length === 0 ? Setup.text.allPassed : Setup.stepsOpen(page._open.length)
        variant: page._open.length ? "heading" : "body"
        tone: page._open.length ? "primary" : "secondary"
    }
    // The steps still open, each a way back to it: first what dictation
    // needs, then what it works without.
    component StepCard: Card {
        id: stepCard
        property var steps: []
        RowList {
            id: openSteps
            values: stepCard.steps
            delegate: SettingRow {
                id: stepRow
                required property string key
                required property int index
                readonly property var step: openSteps.entry(key)
                first: index === 0 && openSteps.first
                title: step.title
                clickable: true
                onClicked: page.choices.goTo(key)
                StatusText { text: stepRow.step.verdict; tone: "warning" }
            }
        }
    }
    readonly property var _needed: _open.filter(s => s.needed)
    readonly property var _helpful: _open.filter(s => !s.needed)
    Reveal {
        open: page._needed.length > 0
        Column {
            width: parent.width
            spacing: Theme.space2
            Txt { width: parent.width; text: Setup.text.needs; variant: "small"; tone: "secondary"; wrapMode: Text.WordWrap }
            StepCard { steps: page._needed }
        }
    }
    Reveal {
        open: page._helpful.length > 0
        Column {
            width: parent.width
            spacing: Theme.space2
            Reveal {
                open: page._needed.length > 0
                Txt {
                    width: parent.width
                    text: Setup.text.worksWithout
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                }
            }
            StepCard { steps: page._helpful }
        }
    }
}
