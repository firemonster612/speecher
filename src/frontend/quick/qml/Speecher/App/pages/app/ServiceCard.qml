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
    // The head's height, and the least it takes, so two cards side by side
    // start their rows at the same height.
    readonly property real naturalHeadHeight: head.implicitHeight + Theme.space4 * 2
    property real minimumHeadHeight: 0
    // Looked up in services, so the card follows every check.
    readonly property var service: Accounts.services.find(s => s.id === serviceId)
    // Signing in through the Codex app or Claude Code, whose fixes the
    // card can name; an API key or CLI Proxy API keeps core's words.
    readonly property bool oauth: serviceId === "openai" ? ["auto", "codex_oauth"].includes(Settings.values.openAiAuthMode)
                                : serviceId === "anthropic" && Settings.values.anthropicAuthMode === "oauth"

    Item {
        width: parent.width
        implicitHeight: Math.max(root.naturalHeadHeight, root.minimumHeadHeight)

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
            Txt { width: parent.width; text: Words.signInDetail(root.service, root.oauth); variant: "small"; tone: "secondary"; wrapMode: Text.WordWrap }
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
