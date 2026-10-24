pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Mock of the sign-ins core finds (host/QuickAccounts in the app). Every
// service has a state (signedIn, expired, missing, checking) and core's line
// on what it found. Secrets are write-only: savedSecrets says which are saved.
//
// --accounts=expired starts with Anthropic's sign-in expired;
// --accounts=none starts with nothing found.
QtObject {
    id: accounts

    readonly property string _start: Launch.arg("accounts", "ok")

    property var services: [
        {
            id: "openai", name: "OpenAI",
            state: _start === "none" ? "missing" : "signedIn",
            detail: _start === "none" ? "No OpenAI credential found" : "Signed in with the Codex app"
        },
        {
            id: "anthropic", name: "Anthropic",
            state: _start === "ok" ? "signedIn" : _start === "expired" ? "expired" : "missing",
            detail: _start === "ok" ? "Signed in with Claude Code"
                  : _start === "expired" ? "Claude login expired; run claude in a terminal and use the /login command"
                  : "Claude credentials were not found"
        },
        {
            id: "cliproxy", name: "CLI Proxy API",
            state: _start === "none" ? "missing" : "signedIn",
            detail: _start === "none" ? "No CLI Proxy API accounts found in ~/.cli-proxy-api"
                                      : "CLI Proxy API accounts found in ~/.cli-proxy-api"
        }
    ]

    // CLI Proxy API's saved account files.
    property var cliproxyAccounts: _start === "none" ? [] : [
        { id: "codex-you@example.com.json", type: "codex", label: "you@example.com", state: "signedIn" },
        { id: "codex-work@acme.io.json", type: "codex", label: "work@acme.io", state: "expired" },
        { id: "claude-you@example.com.json", type: "claude", label: "you@example.com", state: "signedIn" }
    ]

    property var savedSecrets: ({ openAiApiKey: _start !== "none" })
    property var secretErrors: ({})

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
        _pending[id] = "signedIn"
        _set(id, { state: "checking" })
        _recheckTimer.restart()
    }
    function saveSecret(rowId, value) {
        if (value.trim()) savedSecrets = Object.assign({}, savedSecrets, { [rowId]: true })
    }
    function clearSecret(rowId) {
        savedSecrets = Object.assign({}, savedSecrets, { [rowId]: false })
    }

    property var _pending: ({})
    property Timer _recheckTimer: Timer {
        interval: Theme.ms(1100)
        onTriggered: {
            for (const id in accounts._pending)
                accounts._set(id, { state: accounts._pending[id] })
            accounts._pending = {}
        }
    }
    function _set(id, changes) {
        services = services.map(s => s.id === id ? Object.assign({}, s, changes) : s)
    }
}
