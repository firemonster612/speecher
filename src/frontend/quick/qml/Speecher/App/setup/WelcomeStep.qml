pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What Speecher does, shown as the shortcut that starts it, then the
// sign-ins it already found on this computer.
StepPage {
    id: page
    title: Setup.text.welcomeTitle

    readonly property var _found: Accounts.services.filter(s => s.state === "signedIn")

    // The whole idea in one picture: press the shortcut, talk, and the
    // words land where you were typing. Until there is a shortcut, the
    // line says it; where the desktop gives none, the command follows it.
    Plate {
        id: demo
        readonly property var keys: Settings.values.globalShortcut || []
        readonly property bool command: !keys.length && Health.noShortcuts(System.checks)
        implicitHeight: picture.implicitHeight + (keys.length ? 56 : 44)
        height: implicitHeight
        Grow on implicitHeight {}
        Column {
            id: picture
            anchors.centerIn: parent
            width: parent.width - Theme.space6 * 2
            spacing: Theme.space4
            Reveal {
                open: demo.keys.length > 0
                Item {
                    width: parent.width
                    implicitHeight: caps.height + Theme.space2
                    KeyCombo { id: caps; anchors.horizontalCenter: parent.horizontalCenter; keys: demo.keys; size: "large" }
                }
            }
            Txt {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: demo.keys.length ? Setup.text.welcomeHint
                    : demo.command ? Setup.text.welcomeHintCommand
                    : Setup.text.welcomeHintNoKeys
                tone: "secondary"
            }
            Reveal {
                open: demo.command
                Item {
                    width: parent.width
                    implicitHeight: chip.height
                    CommandChip { id: chip; anchors.horizontalCenter: parent.horizontalCenter; command: Schema.row("startCommand").command }
                }
            }
        }
    }

    Txt {
        text: page._found.length ? Setup.text.signInsFound : Setup.text.noSignIns
        variant: page._found.length ? "heading" : "body"
        tone: page._found.length ? "primary" : "secondary"
    }
    Reveal {
        open: page._found.length > 0
        Card {
            RowList {
                id: found
                values: page._found
                delegate: SettingRow {
                    id: foundRow
                    required property string key
                    required property int index
                    readonly property var service: found.entry(key)
                    first: index === 0 && found.first
                    title: service.name
                    help: Words.signInDetail(service)
                    StateText { stateId: foundRow.service.state; text: Words.signInStates[foundRow.service.state] }
                }
            }
        }
    }
}
