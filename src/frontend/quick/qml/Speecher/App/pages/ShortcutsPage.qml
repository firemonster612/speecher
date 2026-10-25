import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Demo
import Speecher.Backend
import Speecher.Wording
import Speecher.App

Page {
    title: Schema.page("shortcuts").title
    subtitle: Schema.page("shortcuts").subtitle

    // The shortcut, big: the page's one picture. Holding the pointer on it
    // presses the keys and listens, as holding the shortcut does.
    Section {
        plain: true
        Plate {
            id: hero
            readonly property bool held: press.pressed
            height: 132
            clip: true

            KeyCombo {
                id: caps
                anchors.horizontalCenter: parent.horizontalCenter
                y: 22
                keys: Settings.values.globalShortcut || []
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

    Section {
        title: Schema.row("globalShortcut").section
        SchemaRow { rowId: "globalShortcut" }
        SchemaRow { rowId: "shortcutBehavior" }
    }

    Section {
        title: Schema.row("pauseShortcut").section
        caption: "These only work while a dictation is in progress, so they can reuse keys other apps have."
        SchemaRow { rowId: "pauseShortcut" }
        SchemaRow { rowId: "cancelShortcut" }
    }

    Section {
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
        SchemaRow {
            id: command
            rowId: "startCommand"
            Rectangle {
                width: cmd.implicitWidth + 44
                height: 30
                radius: Theme.radius
                color: Theme.sheet
                border.width: 1
                border.color: Theme.border
                Txt { id: cmd; x: 12; anchors.verticalCenter: parent.verticalCenter; text: command.spec.command; variant: "code" }
                CopyButton { copiedText: Words.copied; tooltip: Words.copy; size: "small"
; onCopied: System.copy(command.spec.command); anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
}
