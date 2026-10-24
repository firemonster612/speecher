pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The finish: a check that lands once, how to dictate with the chosen
// shortcut, and a way back to any step that still needs attention. The rail
// already lists every step's outcome.
StepPage {
    id: page
    title: ""

    readonly property var _open: page.choices.steps.filter(s => !s.done && s.id !== "ready")

    Column {
        spacing: Theme.space3
        // Finishing setup is a rare moment, so its check springs in.
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
        Txt { text: page.info.title; variant: "display" }
        // The shortcut, and what pressing it does as the Shortcuts page set it.
        Row {
            spacing: Theme.space2
            KeyCombo { keys: Settings.values.globalShortcut || []; anchors.verticalCenter: parent.verticalCenter }
            Txt { text: Schema.valueHelp(Schema.row("shortcutBehavior"), Settings.values.shortcutBehavior); tone: "secondary"; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    Txt {
        text: page._open.length === 0 ? Setup.text.allPassed : Setup.stepsOpen(page._open.length)
        variant: page._open.length ? "heading" : "body"
        tone: page._open.length ? "primary" : "secondary"
    }
    Reveal {
        open: page._open.length > 0
        Card {
            RowList {
                id: openSteps
                values: page._open
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
    }
}
