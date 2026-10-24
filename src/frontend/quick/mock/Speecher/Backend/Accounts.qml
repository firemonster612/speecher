pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Mock of the sign-ins core finds (host/QuickAccounts in the app). Every
// service has a state (signedIn, expired, missing, unknown, checking), the
// state its last check found, core's line on it (possibly empty, for Words to
// word from server and directory), and for OpenAI and Anthropic their speech
// provider's own sign-in. Secrets are write-only: savedSecrets says which are
// saved.
//
// --accounts=expired starts with Anthropic's sign-in expired;
// --accounts=none starts with nothing found.
QtObject {
    id: accounts

    readonly property string _start: Launch.arg("accounts", "ok")
    readonly property string _anthropic: _start === "ok" ? "signedIn" : _start === "expired" ? "expired" : "missing"
    readonly property string _anthropicDetail: _start === "expired" ? "Claude login expired; run claude in a terminal and use the /login command"
                                             : _start === "none" ? "Claude credentials were not found" : ""

    property var services: [
        {
            id: "openai", name: "OpenAI",
            state: _start === "none" ? "missing" : "signedIn", found: _start === "none" ? "missing" : "signedIn",
            detail: _start === "none" ? "No OpenAI credential found" : "Signed in with the Codex app",
            server: "", directory: "",
            speech: { state: _start === "none" ? "missing" : "signedIn",
                      detail: _start === "none" ? "The Codex app has no sign-in to reuse" : "" }
        },
        {
            id: "anthropic", name: "Anthropic",
            state: _anthropic, found: _anthropic, detail: _anthropicDetail, server: "", directory: "",
            speech: { state: _anthropic, detail: _anthropicDetail }
        },
        {
            id: "cliproxy", name: "CLI Proxy API",
            state: _start === "none" ? "missing" : "signedIn", found: _start === "none" ? "missing" : "signedIn",
            detail: "", server: "", directory: "~/.cli-proxy-api"
        }
    ]

    // CLI Proxy API's saved account files.
    property var cliproxyAccounts: _start === "none" ? [] : [
        { id: "codex-you@example.com.json", type: "codex", label: "you@example.com", state: "signedIn" },
        { id: "codex-work@acme.io.json", type: "codex", label: "work@acme.io", state: "expired" },
        { id: "claude-you@example.com.json", type: "claude", label: "you@example.com", state: "signedIn" }
    ]

    property var savedSecrets: ({ openAiApiKey: _start !== "none" })
    property var pendingClears: ({})
    property var secretErrors: ({})

    signal secretSaved(string rowId)

    // Asks core to read the service's credentials again. Several checks can
    // run at once; each remembers the state it started from.
    function recheck(id) {
        const before = services.find(s => s.id === id)
        if (before.state !== "checking") _pending[id] = before.state
        _set(id, { state: "checking" })
        _recheckTimer.restart()
    }
    // Renews an expired sign-in: here it always works.
    function renew(id) {
        _renewed[id] = true
        _pending[id] = "signedIn"
        _set(id, { state: "checking" })
        _recheckTimer.restart()
    }
    function saveSecret(rowId, value) {
        if (!value.trim()) return
        savedSecrets = Object.assign({}, savedSecrets, { [rowId]: true })
        secretSaved(rowId)
    }
    function clearSecret(rowId) {
        savedSecrets = Object.assign({}, savedSecrets, { [rowId]: false })
    }

    property var _pending: ({})
    property var _renewed: ({})
    property Timer _recheckTimer: Timer {
        interval: Theme.ms(1100)
        onTriggered: {
            for (const id in accounts._pending) {
                const found = accounts._pending[id]
                accounts._set(id, accounts._renewed[id]
                    ? { state: found, found: found, detail: id === "openai" ? "Signed in with the Codex app" : "",
                        speech: { state: found, detail: "" } }
                    : { state: found })
            }
            accounts._pending = {}
            accounts._renewed = {}
        }
    }
    function _set(id, changes) {
        services = services.map(s => s.id === id ? Object.assign({}, s, changes) : s)
    }
}
