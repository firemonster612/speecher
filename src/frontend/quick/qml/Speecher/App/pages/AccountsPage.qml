pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "app"

// The services side by side, each a card with its state on top so every
// sign-in reads at a glance, and CLI Proxy API below with the account files
// it found.
Page {
    id: page
    title: Schema.page("accounts").title
    subtitle: Schema.page("accounts").subtitle
    headerActions: [
        Button {
            text: Words.checkAll
            iconName: "refresh-cw"
            onClicked: Accounts.services.forEach(s => Accounts.recheck(s.id))
        }
    ]

    // The two cards end together, and their rows start together; rows that
    // come and go with the sign-in source ease both cards' height.
    Row {
        id: pair
        width: parent.width
        spacing: Theme.space4
        readonly property real _cardWidth: (width - spacing) / 2
        readonly property real _cardHeight: Math.max(openAi.implicitHeight, anthropic.implicitHeight)

        ServiceCard {
            id: openAi
            serviceId: "openai"
            width: pair._cardWidth
            height: pair._cardHeight
            minimumHeadHeight: anthropic.naturalHeadHeight
            SchemaRow { rowId: "openAiAuthMode"; stacked: true }
            SchemaRow { objectName: "openAiApiKeyRow"; rowId: "openAiApiKey"; stacked: true; shown: Settings.values.openAiAuthMode === "settings" }
            SchemaRow {
                rowId: "openAiCliproxyAccount"
                stacked: true
                shown: Settings.values.openAiAuthMode === "cliproxy"
                AccountSelect { rowId: "openAiCliproxyAccount"; type: "codex"; width: parent.width }
            }
        }
        ServiceCard {
            id: anthropic
            serviceId: "anthropic"
            width: pair._cardWidth
            height: pair._cardHeight
            minimumHeadHeight: openAi.naturalHeadHeight
            SchemaRow { rowId: "anthropicAuthMode"; stacked: true }
            SchemaRow {
                rowId: "anthropicCliproxyAccount"
                stacked: true
                shown: Settings.values.anthropicAuthMode === "cliproxy"
                AccountSelect { rowId: "anthropicCliproxyAccount"; type: "claude"; width: parent.width }
            }
        }
    }

    ServiceCard {
        serviceId: "cliproxy"

        // The account files it found, each with its own state.
        RowList {
            id: accountFiles
            values: Accounts.cliproxyAccounts
            delegate: ListRow {
                id: accountRow
                required property string key
                required property int index
                readonly property var account: accountFiles.entry(key)
                first: index === 0 && accountFiles.first
                iconName: "file-key"
                title: account.label
                detail: account.id
                trailing: StateText {
                    stateId: accountRow.account.state
                    text: Words.signInStates[accountRow.account.state]
                    indicator: "dot"
                }
            }
        }
        SchemaRow { rowId: "cliproxyOauthDir" }
        SchemaRow { rowId: "cliproxyBaseUrl" }
        SchemaRow { rowId: "cliproxyApiKey" }
    }
}
