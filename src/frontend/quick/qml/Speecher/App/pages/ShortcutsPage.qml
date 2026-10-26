import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Demo
import Speecher.Backend
import Speecher.Wording
import Speecher.App

Page {
    id: page
    // The desktop gives Speecher no shortcut: the command is the way in, so
    // the page says so and leads with it.
    readonly property bool noShortcuts: Health.noShortcuts(System.checks)
    title: Schema.page("shortcuts").title
    subtitle: noShortcuts ? Schema.page("shortcuts").subtitleNoShortcuts : Schema.page("shortcuts").subtitle

    // How shortcuts reach Speecher: first where the command is the way in,
    // last where it is the extra.
    component Reach: Section {
        title: Schema.row("startCommand").section
        // The same check as Permissions & health's, and where the desktop lists the shortcuts.
        HealthRow {
            rowId: "globalShortcuts"
            // Where Speecher knows the desktop's own shortcut settings.
            Button {
                visible: System.canOpenShortcutSettings
                text: "System settings"
                size: "small"
                trailingIcon: "external-link"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: System.openShortcutSettings()
            }
        }
        StartCommandRow {}
    }
    Reveal { open: page.noShortcuts; Reach {} }

    // The shortcut, big: the page's one picture. Holding the pointer on it
    // presses the keys and listens, as holding the shortcut does. With no
    // shortcut there is nothing to picture, and the line of what pressing
    // it does is the chosen option's own help below, so it folds away.
    Reveal {
        open: hero.keys.length > 0
        Plate {
            id: hero
            readonly property bool held: press.pressed
            readonly property var keys: Settings.values.globalShortcut || []
            height: 132
            clip: true

            KeyCombo {
                id: caps
                anchors.horizontalCenter: parent.horizontalCenter
                y: 22
                keys: hero.keys
                size: "large"
                pressed: hero.held
                lit: hero.held
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: caps.bottom
                anchors.topMargin: 22
                spacing: 10
                Row {
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: hero.held ? Theme.negative : Theme.textTertiary
                        anchors.verticalCenter: parent.verticalCenter
                        Behavior on color { EaseColor {} }
                    }
                    Waveform { bars: 16; height: 16; barWidth: 2.5; gap: 2; level: voice.level; active: hero.held; anchors.verticalCenter: parent.verticalCenter }
                }
                // What pressing it does, as the chosen option says it.
                MorphText {
                    text: Schema.valueHelp(Schema.row("shortcutBehavior"), Settings.values.shortcutBehavior)
                    tone: "secondary"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            DemoVoice { id: voice; running: hero.held }
            TapHandler { id: press; cursorShape: Qt.PointingHandCursor }
        }
    }

    // The dictation shortcut and what pressing it does: with no shortcut to
    // be had, the command above runs one dictation, so both fold away.
    Reveal {
        open: !page.noShortcuts
        Section {
            title: Schema.row("globalShortcut").section
            SchemaRow { rowId: "globalShortcut" }
            SchemaRow { rowId: "shortcutBehavior" }
        }
    }

    // Keys that act while dictating bind through the desktop too, so they
    // fold away with the shortcut where it gives none.
    Reveal {
        open: !page.noShortcuts
        Section {
            title: Schema.row("pauseShortcut").section
            caption: "These only work while a dictation is in progress, so they can reuse keys other apps have."
            SchemaRow { rowId: "pauseShortcut" }
            SchemaRow { rowId: "cancelShortcut" }
        }
    }

    Reveal { open: !page.noShortcuts; Reach {} }
}
