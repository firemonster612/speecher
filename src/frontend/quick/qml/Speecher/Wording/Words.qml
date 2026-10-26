pragma Singleton
import QtQuick

// The UI's shared wording: the words many places use, such as the labels and
// tooltips pages hand to shared controls, which have none of their own, the
// shell's words (the window, the sidebar and the search palette), and the
// helpers that word numbers and lists.
QtObject {
    // "1,284"
    function num(n, decimals) { return Number(n).toLocaleString(Qt.locale(), "f", decimals || 0) }
    // "1 term", "128 terms"
    function count(n, one, many) { return num(n) + " " + (n === 1 ? one : many) }
    // "42%", from 0.42
    function percent(fraction) { return Math.round(fraction * 100) + "%" }
    // "Slack, Linear and Notion"
    function list(names) { return names.length < 2 ? names.join("") : names.slice(0, -1).join(", ") + " and " + names[names.length - 1] }

    readonly property string cancel: "Cancel"
    readonly property string save: "Save"
    readonly property string edit: "Edit"
    readonly property string delete_: "Delete"
    readonly property string remove: "Remove"
    readonly property string undo: "Undo"
    readonly property string cantUndo: "This can't be undone."
    readonly property string moveUp: "Move up"
    readonly property string moveDown: "Move down"
    readonly property string copy: "Copy"
    readonly property string copied: "Copied"
    readonly property string show: "Show"
    readonly property string hide: "Hide"
    readonly property string clear: "Clear"
    readonly property string choose: "Choose…"
    // A settings row this version doesn't save yet, greyed.
    readonly property string notSavedYet: "Not available in this version yet."
    readonly property string suggestions: "Suggestions"
    readonly property string turnOn: "Turn on"
    readonly property string checkAgain: "Check again"
    // The language picker's search, and what it says when nothing matches.
    readonly property string searchLanguages: "Search languages"
    readonly property string noLanguage: "No language matches."
    readonly property string more: "More"
    readonly property string pause: "Pause"
    readonly property string resume: "Resume"
    // A shortcut field: unset, listening for keys, and its clear button.
    readonly property string notSet: "Not set"
    // A shortcut row where the desktop gives Speecher no shortcut.
    readonly property string noShortcutsHere: "Not available on this desktop."
    readonly property string pressShortcut: "Press a shortcut…"
    readonly property string clearShortcut: "Clear shortcut"
    // A secret field: what it shows once a key is saved, and why a save or
    // clear failed (Accounts.secretErrors).
    readonly property string savedSecret: "Saved"
    function secretError(error) {
        const what = error.action === "clear" ? "Not cleared" : "Not saved"
        return error.reason ? what + ": " + error.reason : what
    }
    // A sign-in's state, on Accounts and in setup.
    readonly property var signInStates: ({
        signedIn: "Signed in", expired: "Expired", missing: "Not signed in", unknown: "Unknown",
        disabled: "Disabled", unusable: "Unusable", checking: "Checking"
    })
    // A service's line on Accounts: what to do when the keyring didn't
    // answer or, signing in through the Codex app or Claude Code (oauth),
    // a sign-in is missing or expired; else core's own words, else where
    // its last check found the sign-in (Accounts.services).
    function signInDetail(service, oauth) {
        if (service.state === "unknown") return "Speecher couldn't read the system keychain. Check again in a moment."
        const fix = oauth && ({
            openai: { missing: "No ChatGPT sign-in found. Sign in with the Codex app, or choose another sign-in below.",
                      expired: "Your ChatGPT sign-in has expired. Sign in again in the Codex app." },
            anthropic: { missing: "Claude Code isn't signed in. Use its desktop app or /login in the claude CLI.",
                         expired: "Claude Code's sign-in has expired. Sign in again, or use /login in the claude CLI." }
        }[service.id] || {})[service.state]
        if (fix) return fix
        if (service.detail) return service.detail
        if (service.server) return "Using CLI Proxy API at " + service.server
        if (service.id === "anthropic" && service.found === "signedIn")
            return service.directory ? "CLI Proxy API Claude token found" : "Signed in with Claude Code"
        if (service.id !== "cliproxy" || !service.directory) return ""
        const found = { signedIn: "CLI Proxy API accounts found in %1", expired: "Every CLI Proxy API account in %1 is expired",
                        missing: "No CLI Proxy API accounts found in %1" }[service.found]
        return found ? found.arg(service.directory) : ""
    }
    readonly property string disabledInCliproxy: "Disabled in CLI Proxy API"
    readonly property string checkAll: "Check all"
    readonly property string signInAgain: "Sign in again"
    // What each service on Accounts is used for, by its id.
    readonly property var serviceUses: ({
        openai: "Used for dictation and cleanup.",
        anthropic: "Used for dictation and cleanup.",
        cliproxy: "Only needed when a service above signs in through it, or cleanup goes through its server."
    })

    readonly property var shell: ({
        appName: "Speecher",
        searchSettings: "Search settings",
        searchPlaceholder: "Search settings and pages",
        suggested: "Suggested",
        page: "Page",
        showSubpages: "Show subpages",
        hideSubpages: "Hide subpages",
        searchTip: "Try a setting's name, a service such as Claude, or a word like hotkey.",
        moveHint: "Move",
        openHint: "Open",
        closeHint: "Close",
        minimize: "Minimize",
        maximize: "Maximize",
        restore: "Restore",
        zoom: "Zoom",
        close: "Close"
    })
    // The updater's states and actions, as the Updates page and the popup's
    // update offer both word them. restart and restarting are keyed by what
    // the restart waits for (Updates.restartBlocker).
    readonly property var updates: ({
        available: v => "Speecher " + v + " is available",
        stableReplacement: v => "Switch to Stable Release " + v,
        openReleasePage: "Open release page",
        downloading: v => "Downloading Speecher " + v,
        ready: "Restart to finish updating",
        restart: ({ "": "Restart now", dictation: "Restart after this dictation", recording: "Restart after this recording" }),
        restarting: ({ "": "Restarting…", dictation: "Restarting after this dictation…", recording: "Restarting after this recording…" }),
        later: "Later",
        checkFailed: "Update check failed",
        tryAgain: "Try again"
    })
    // The toast after deleting something a person made, which offers Undo.
    function deleted(name) { return "Deleted “" + name + "”" }
    function version(v) { return "Version " + v }
    function updateReady(v) { return "Restart to update to " + v }
    function noMatches(query) { return "Nothing matches “" + query + "”" }
    function results(n) { return count(n, "result", "results") }
}
