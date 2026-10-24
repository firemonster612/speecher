.pragma library

// How each of System.checks reads: by the check's id and reason, the short
// status, the line under it and its fix button's label, where it has one.

var words = {
    microphoneAccess: {
        notAsked: { status: "Allowed", detail: "Your desktop lets apps record without asking." }
    },
    desktopAccessibility: {
        unsupported: { status: "Not available", detail: "This Speecher build does not include desktop accessibility." },
        off: { status: "Off", detail: "Speecher cannot tell which app you are in, so paste rules and context are limited.", fix: "Turn on" },
        sessionOnly: { status: "On for now", detail: "It turns off when you sign out, and paste rules and context with it.", fix: "Keep on" },
        on: { status: "On", detail: "Paste rules, context and learning can see the app you are in." }
    },
    globalShortcuts: {
        checking: { status: "Checking…", detail: "" },
        unsupported: { status: "Not available", detail: "Your desktop gives Speecher no shortcut, so start dictation with the speecher command." },
        notSet: { status: "Not set", detail: "Choose a shortcut on the Shortcuts page." },
        registered: { status: "Registered", detail: "Your desktop delivers the shortcut in every app." }
    },
    virtualKeyboardHealth: {
        settingUp: { status: "Setting up…", detail: "Your computer asks for your password to finish." },
        starting: { status: "Starting…", detail: "" },
        removing: { status: "Removing…", detail: "Your computer asks for your password to finish." },
        testing: { status: "Testing", detail: "Run the typing test to turn it on." },
        disabled: { status: "Off", detail: "Set up, but turned off in Speecher, so Speecher copies to the clipboard.", fix: "Turn on" },
        availableOutside: { status: "Not set up", detail: "ydotool is installed, but Speecher hasn't set it up. Setup asks for your password once.", fix: "Set up" },
        notInstalled: { status: "Not set up", detail: "Speecher can only copy to the clipboard. Setup asks for your password once.", fix: "Set up" },
        needsUinputPermission: { status: "Needs permission", detail: "It can't reach /dev/uinput yet. Setup fixes that and asks for your password once.", fix: "Set up" },
        needsSignOut: { status: "Sign out first", detail: "Sign out and back in so this session can use the virtual keyboard." },
        daemonNotRunning: { status: "Stopped", detail: "Set up, but its service isn't running, so Speecher copies to the clipboard.", fix: "Start" },
        ready: { status: "Ready", detail: "Speecher types into other apps directly." },
        readyLayoutCaveat: { status: "Ready", detail: "Speecher types into other apps directly, as on a US keyboard layout." }
    }
};

function _words(check) { return (words[check.id] || {})[check.reason] || { status: "", detail: "" }; }

function status(check) { return _words(check).status; }
function detail(check) { return _words(check).detail; }
// The fix button's label, or "" where the check offers none.
function fix(check) { return check.fix ? _words(check).fix || "" : ""; }

// The caption over the checks: "All 4 checks pass." or "3 of 4 checks pass."
function summary(checks) {
    const passing = checks.filter(c => c.state === "ok").length;
    return (passing === checks.length ? "All " + checks.length : passing + " of " + checks.length) + " checks pass.";
}
