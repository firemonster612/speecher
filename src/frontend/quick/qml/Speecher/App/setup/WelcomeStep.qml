pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// What Speecher does, shown as the shortcut that starts it, then the
// sign-ins it already found on this computer.
StepPage {
    id: page
    title: Setup.text.welcomeTitle

    readonly property var _found: Accounts.services.filter(s => s.state === "signedIn")

    // The whole idea in one picture: press the shortcut, talk, and the
    // words land where you were typing.
    Plate {
        height: 156
        KeyCombo {
            id: caps
            anchors.horizontalCenter: parent.horizontalCenter
            y: 28
            keys: Settings.values.globalShortcut || []
            size: "large"
        }
        Txt {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: caps.bottom
            anchors.topMargin: Theme.space5
            text: Setup.text.welcomeHint
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
                    help: service.detail
                    StateText { stateId: foundRow.service.state; text: Accounts.stateLabels[foundRow.service.state]; indicator: "dot" }
                }
            }
        }
    }
}
