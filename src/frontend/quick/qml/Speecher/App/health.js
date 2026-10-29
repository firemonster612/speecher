.pragma library

// How each of System.checks reads: by the check's id and reason, the short
// status, the line under it and its fix button's label, where it has one;
// and by its failures' steps, what went wrong.

var words = {
    microphoneAccess: {
        notAsked: { status: "Allowed", detail: "Your desktop lets apps record without asking." },
        allowed: { status: "Allowed", detail: "Speecher can use your microphone." },
        notDetermined: { status: "Not asked yet", detail: "macOS asks the first time Speecher listens." },
        denied: { status: "Off", detail: "Turned off for Speecher in Privacy & Security, so dictation hears nothing.", fix: "Open settings" },
        restricted: { status: "Blocked", detail: "A profile or Screen Time on this Mac keeps apps from the microphone." },
        deviceOff: { status: "Off", detail: "Microphone access is off for this device in Windows' privacy settings.", fix: "Open settings" },
        appsOff: { status: "Off", detail: "Windows' privacy settings keep every app from the microphone.", fix: "Open settings" },
        desktopAppsOff: { status: "Off", detail: "Windows' privacy settings keep desktop apps, Speecher among them, from the microphone.", fix: "Open settings" }
    },
    desktopAccessibility: {
        unsupported: { status: "Not available", detail: "This Speecher build does not include desktop accessibility." },
        off: { status: "Off", detail: "Speecher can't tell which app you are in, so paste rules and context are limited.", fix: "Turn on" },
        sessionOnly: { status: "On for now", detail: "It turns off when you sign out, and paste rules and context with it.", fix: "Keep on" },
        on: { status: "On", detail: "Paste rules, context and learning can see the app you are in." }
    },
    globalShortcuts: {
        checking: { status: "Checking…", detail: "" },
        unsupported: { status: "Not available", detail: "Your desktop gives Speecher no shortcut, so start dictation with the speecher command." },
        notSet: { status: "Not set", detail: "Choose a shortcut in Dictation › Shortcuts." },
        registered: { status: "Registered", detail: "Your desktop delivers the shortcut in every app." }
    },
    virtualKeyboardHealth: {
        settingUp: { status: "Setting up…", detail: "Your computer asks for your password to finish." },
        starting: { status: "Starting…", detail: "" },
        removing: { status: "Removing…", detail: "Your computer asks for your password to finish." },
        testing: { status: "Testing", detail: "Run the typing test to turn it on." },
        disabled: { status: "Off", detail: "Set up, but turned off in Speecher, so it can't press paste keys.", fix: "Turn on" },
        availableOutside: { status: "Not set up", detail: "ydotool is installed, but Speecher hasn't set it up. Setup asks for your password once.", fix: "Set up" },
        notInstalled: { status: "Not set up", detail: "Without it, Speecher can't press paste keys. Setup asks for your password once.", fix: "Set up" },
        needsUinputPermission: { status: "Needs permission", detail: "It can't reach /dev/uinput yet. Setup fixes that and asks for your password once.", fix: "Set up" },
        needsSignOut: { status: "Sign out first", detail: "Sign out and back in so this session can use the virtual keyboard." },
        daemonNotRunning: { status: "Stopped", detail: "Set up, but its service isn't running, so Speecher can't press paste keys.", fix: "Start" },
        ready: { status: "Ready", detail: "Speecher types into other apps directly." },
        readyLayoutCaveat: { status: "Ready", detail: "Speecher types into other apps directly, as on a US keyboard layout." }
    }
};

// What went wrong in a fix, by the check's id and the failure's step.
var failures = {
    desktopAccessibility: { enable: "Desktop accessibility could not be turned on" },
    virtualKeyboardHealth: {
        setUp: "Setup failed",
        start: "The virtual keyboard service could not start",
        type: "The virtual keyboard could not type",
        stop: "The virtual keyboard service could not be stopped",
        remove: "The virtual keyboard setup could not be removed",
        filesLeft: "The removal finished, but some of the files Speecher set up are still there"
    }
};

// The check with this id. Pass System.checks itself, so a binding that asks
// follows the checks as they change.
function find(checks, id) { return checks.find(c => c.id === id) || {}; }

// Whether the desktop gives Speecher no global shortcut, so the start
// command is the way to start dictation. Pass System.checks itself.
function noShortcuts(checks) { return find(checks, "globalShortcuts").reason === "unsupported"; }

function _words(check) { return (words[check.id] || {})[check.reason] || { status: "", detail: "" }; }

function status(check) { return _words(check).status; }
function detail(check) { return _words(check).detail; }
// The fix button's label, or "" where the check offers none.
function fix(check) { return check.fix ? _words(check).fix || "" : ""; }
// What went wrong the last time something was tried on the check, or "":
// "Setup failed: Authorization was declined."
function failure(check) {
    return (check.failures || []).map(f => {
        const what = (failures[check.id] || {})[f.step] || "";
        return f.error ? what + ": " + f.error : what + ".";
    }).join(" ");
}

// The caption over the checks: "All 4 checks pass." or "3 of 4 checks pass."
// A check that can't pass on this desktop and has a way round, so there is
// nothing to fix: no global shortcut, where the start command works. It is
// said in plain ink and not counted.
function settledHere(check) { return check.id === "globalShortcuts" && check.reason === "unsupported"; }

function summary(checks) {
    const counted = checks.filter(c => !settledHere(c));
    const passing = counted.filter(c => c.state === "ok").length;
    const said = (passing === counted.length ? "All " + counted.length : passing + " of " + counted.length) + " checks pass.";
    return counted.length < checks.length ? said + " Global shortcuts aren't offered on this desktop." : said;
}
