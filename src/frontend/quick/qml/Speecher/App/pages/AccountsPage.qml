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
// it found, its settings folded away until a sign-in uses it.
Page {
    id: page
    // A search for the API key while another sign-in is chosen, which hides
    // the key's row: it lands on the sign-in it would be chosen in.
    property bool keySearched: false
    readonly property bool _keyHidden: Settings.values.openAiAuthMode !== Accounts.authModes.openAi.settings
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
            // Automatic names what it found, once it found something.
            SchemaRow {
                id: openAiMode
                readonly property var found: spec.options.find(o => o.id === openAi.service.source)
                rowId: "openAiAuthMode"
                stacked: true
                note: (page.keySearched && page._keyHidden ? spec.keyHint + " " : "")
                      + (Settings.values.openAiAuthMode === Accounts.authModes.openAi.auto && found
                         ? spec.autoFound.arg(found.label) + " " + spec.help : spec.help)
                // Never switches the sign-in for the search.
                SearchLanding {
                    rowIds: page._keyHidden ? ["openAiApiKey"] : []
                    target: openAiMode
                    onLanded: { page.keySearched = true; openAiMode.highlight() }
                }
            }
            SchemaRow { objectName: "openAiApiKeyRow"; rowId: "openAiApiKey"; stacked: true; shown: Settings.values.openAiAuthMode === Accounts.authModes.openAi.settings }
            SchemaRow {
                rowId: "openAiCliproxyAccount"
                stacked: true
                shown: Settings.values.openAiAuthMode === Accounts.authModes.openAi.cliproxy
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
                shown: Settings.values.anthropicAuthMode === Accounts.authModes.anthropic.cliproxy
                AccountSelect { rowId: "anthropicCliproxyAccount"; type: "claude"; width: parent.width }
            }
        }
    }

    ServiceCard {
        id: cliproxy
        serviceId: "cliproxy"
        // Its account files matter once a sign-in reads them; until then
        // the card offers only its server, folded away.
        readonly property bool used: service.inUse || !!Settings.values.cliproxyOauthDir

        // The account files it found, each with its own state.
        RowList {
            id: accountFiles
            values: cliproxy.used ? Accounts.cliproxyAccounts : []
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
                }
            }
        }
        SchemaRow { rowId: "cliproxyOauthDir"; shown: cliproxy.used }
        // Open from the start once a sign-in uses it.
        Disclosure {
            id: server
            objectName: "cliproxyServer"
            title: Schema.row("cliproxyBaseUrl").foldedLabel
            help: Schema.row("cliproxyBaseUrl").foldedHelp
            trailingText: Settings.values.cliproxyBaseUrl || ""
            open: cliproxy.used
            SearchLanding {
                rowIds: ["cliproxyBaseUrl", "cliproxyApiKey"]
                target: server
                onLanded: { server.open = true; server.highlight() }
            }
            SchemaRow { rowId: "cliproxyBaseUrl" }
            SchemaRow { rowId: "cliproxyApiKey" }
        }
    }
}
