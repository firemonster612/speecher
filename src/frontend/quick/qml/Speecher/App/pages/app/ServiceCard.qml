import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One service on Accounts as a card: its name and sign-in state at the top,
// what Speecher found and what the service is used for, then the service's
// settings rows. An expired sign-in offers to renew it; any state can be
// checked again. Rows declared inside follow the head and rule themselves off
// from it.
Card {
    id: root

    required property string serviceId
    // Looked up in services, so the card follows every check.
    readonly property var service: Accounts.services.find(s => s.id === serviceId)

    Item {
        width: parent.width
        implicitHeight: head.implicitHeight + Theme.space4 * 2

        Column {
            id: head
            x: Theme.rowInset
            y: Theme.space4
            width: parent.width - Theme.rowInset * 2
            spacing: 3

            Item {
                width: parent.width
                height: Math.max(name.implicitHeight, recheck.height)
                Txt { id: name; text: root.service.name; variant: "heading"; anchors.verticalCenter: parent.verticalCenter }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space1
                    StateText {
                        stateId: root.service.state
                        text: Words.signInStates[root.service.state]
                        indicator: "dot"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    IconButton {
                        id: recheck
                        iconName: "refresh-cw"
                        tooltip: Words.checkAgain
                        size: 26
                        enabled: !States.busy(root.service.state)
                        onClicked: Accounts.recheck(root.service.id)
                    }
                }
            }
            Txt { width: parent.width; text: root.service.detail; variant: "small"; tone: "secondary"; wrapMode: Text.WordWrap }
            Txt { width: parent.width; text: Words.serviceUses[root.service.id] || ""; variant: "small"; tone: "tertiary"; wrapMode: Text.WordWrap }
            Reveal {
                open: root.service.state === "expired"
                Item {
                    width: parent.width
                    implicitHeight: signIn.height + Theme.space2
                    Button {
                        id: signIn
                        y: Theme.space2
                        text: Words.signInAgain
                        variant: "primary"
                        size: "small"
                        onClicked: Accounts.renew(root.service.id)
                    }
                }
            }
        }
    }
}
