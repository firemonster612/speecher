pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// ChatGPT or Claude on Services, and in a Fix sheet: its sign-in state, and
// Sign in with, the one choice of where the sign-in comes from for both its
// speech and its refinement. CLI Proxy API is one of those choices; picked,
// the card shows its account, the directory the account files are in and
// the optional server cleanup goes through, with that server's key.
ServiceCard {
    id: card

    // "openai" or "anthropic", Accounts' ids for ChatGPT and Claude.
    serviceId: "openai"
    readonly property bool openAi: serviceId === "openai"
    readonly property string modeRow: openAi ? "openAiAuthMode" : "anthropicAuthMode"
    readonly property bool cliproxy: Settings.values[modeRow] === (openAi ? Accounts.authModes.openAi.cliproxy
                                                                          : Accounts.authModes.anthropic.cliproxy)
    readonly property bool keyHidden: openAi && Settings.values.openAiAuthMode !== Accounts.authModes.openAi.settings
    // A search for the API key while another sign-in hides its row: it
    // lands on the sign-in it would be chosen in.
    property bool keySearched: false

    // Automatic names what it found, once it found something.
    SchemaRow {
        id: mode
        readonly property var found: spec.options.find(o => o.id === card.service.source)
        rowId: card.modeRow
        stacked: true
        note: (card.keySearched && card.keyHidden ? spec.keyHint + " " : "")
              + (card.openAi && Settings.values.openAiAuthMode === Accounts.authModes.openAi.auto && found
                 ? spec.autoFound.arg(found.label) + " " + spec.help : spec.help)
        // Never switches the sign-in for the search.
        SearchLanding {
            rowIds: card.keyHidden ? ["openAiApiKey"] : []
            target: mode
            onLanded: { card.keySearched = true; mode.highlight() }
        }
    }
    SchemaRow {
        objectName: "openAiApiKeyRow"
        rowId: "openAiApiKey"
        stacked: true
        shown: card.openAi && !card.keyHidden
    }
    SchemaRow {
        id: account
        rowId: card.openAi ? "openAiCliproxyAccount" : "anthropicCliproxyAccount"
        stacked: true
        shown: card.cliproxy
        AccountSelect { rowId: account.rowId; type: card.openAi ? "codex" : "claude"; width: parent.width }
    }
    SchemaRow { rowId: "cliproxyOauthDir"; stacked: true; shown: card.cliproxy }
    SchemaRow { rowId: "cliproxyBaseUrl"; stacked: true; shown: card.cliproxy }
    SchemaRow { rowId: "cliproxyApiKey"; stacked: true; shown: card.cliproxy && !!Settings.values.cliproxyBaseUrl }
}
