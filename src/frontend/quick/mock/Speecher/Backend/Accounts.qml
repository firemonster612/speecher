pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Placeholder: the app ships this mock until its wiring PR replaces it.
// Mock of the sign-ins core finds for each service. Every service has a
// state (signedIn, expired, missing, checking), a line on what was found and
// what it is used for, in wording core will supply.
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
            detail: _start === "none" ? "Sign in to the Codex app, or save an OpenAI API key." : "you@example.com, from the Codex app, Plus plan",
            use: "Used for dictation and cleanup."
        },
        {
            id: "anthropic", name: "Anthropic",
            state: _start === "ok" ? "signedIn" : _start === "expired" ? "expired" : "missing",
            detail: _start === "ok" ? "you@example.com, from Claude Code, Max plan"
                  : _start === "expired" ? "Run /login in the claude CLI, or open the Claude desktop app."
                  : "Sign in to Claude Code in its desktop app, or with /login in the claude CLI.",
            use: "Used for Claude Voice dictation and Anthropic cleanup."
        },
        {
            id: "cliproxy", name: "CLI Proxy API",
            state: _start === "none" ? "missing" : "signedIn",
            detail: _start === "none" ? "Point Account directory at your CLI Proxy API folder." : "In ~/.cli-proxy-api, found automatically",
            use: "Only needed when a service above signs in through it, or cleanup goes through its server."
        }
    ]

    readonly property var text: ({ checkAll: "Check all", signInAgain: "Sign in again" })
    // The short word a state shows as.
    readonly property var stateLabels: ({ signedIn: "Signed in", expired: "Expired", missing: "Not found", checking: "Checking" })

    // CLI Proxy API's saved account files.
    property var cliproxyAccounts: _start === "none" ? [] : [
        { id: "codex-you@example.com.json", type: "codex", label: "you@example.com", plan: "ChatGPT Plus", state: "signedIn" },
        { id: "codex-work@acme.io.json", type: "codex", label: "work@acme.io", plan: "ChatGPT Team", state: "expired" },
        { id: "claude-you@example.com.json", type: "claude", label: "you@example.com", plan: "Claude Max", state: "signedIn" }
    ]

    function service(id) { return services.find(s => s.id === id) }

    // Asks core to read the service's credentials again. Several checks can
    // run at once; each remembers the state it started from.
    function recheck(id) {
        const before = service(id)
        if (before.state !== "checking") _pending[id] = before.state
        _set(id, { state: "checking" })
        _recheckTimer.restart()
    }

    property var _pending: ({})
    property Timer _recheckTimer: Timer {
        interval: Theme.ms(1100)
        onTriggered: {
            for (const id in accounts._pending)
                accounts._set(id, accounts._pending[id] === "expired"
                    ? { state: "signedIn", detail: "you@example.com, from Claude Code, Max plan" }
                    : { state: accounts._pending[id] })
            accounts._pending = {}
        }
    }
    function _set(id, changes) {
        services = services.map(s => s.id === id ? Object.assign({}, s, changes) : s)
    }
}
