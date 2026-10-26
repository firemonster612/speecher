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
    // line alone says it.
    Plate {
        id: demo
        readonly property var keys: Settings.values.globalShortcut || []
        // With no keys, as tall as the line, which can wrap.
        implicitHeight: keys.length ? 156 : hint.implicitHeight + 44
        height: implicitHeight
        Grow on implicitHeight {}
        KeyCombo {
            id: caps
            anchors.horizontalCenter: parent.horizontalCenter
            y: 28
            keys: demo.keys
            size: "large"
            opacity: demo.keys.length ? 1 : 0
            Behavior on opacity { Ease {} }
        }
        Txt {
            id: hint
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: demo.keys.length ? 34 : 22
            width: parent.width - Theme.space6 * 2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: demo.keys.length ? Setup.text.welcomeHint
                : Health.find(System.checks, "globalShortcuts").reason === "unsupported" ? Setup.text.welcomeHintCommand
                : Setup.text.welcomeHintNoKeys
            tone: "secondary"
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
                    StateText { stateId: foundRow.service.state; text: Words.signInStates[foundRow.service.state]; indicator: "dot" }
                }
            }
        }
    }
}
