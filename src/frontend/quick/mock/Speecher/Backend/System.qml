pragma Singleton
import QtQuick
import Speecher.Wording
import Speecher.Launch
import Speecher.Theme

// Placeholder: the app ships this mock until its wiring PR replaces it.
// Mock of what Speecher knows about the computer it runs on: the permissions
// dictation needs, phone transfer, what it hands to the desktop, removing
// it, and the setup assistant's steps. Wording comes from core.
//
// --health=issues fails three of the permission checks.
QtObject {
    id: system

    readonly property string _health: Launch.arg("health", "default")

    // Permissions and health --------------------------------------------
    // Desktop accessibility and the virtual keyboard, which pages check
    // before offering what needs them. --accessibility=off and
    // --virtual-keyboard=stopped|notSetUp start them otherwise.
    property bool accessibility: _health !== "issues" && Launch.arg("accessibility", "on") !== "off"
    property string virtualKeyboard: _health === "issues" ? "notSetUp" : Launch.arg("virtual-keyboard", "running")   // running, stopped, notSetUp
    property bool _microphoneBlocked: _health === "issues"

    // One entry per general.js health row, by row id, made from the states
    // above. state: ok, attention, off, working. status is the short word,
    // detail the line under it.
    readonly property var checks: [
        _fixing.includes("microphoneAccess") ? _working("microphoneAccess")
            : _microphoneBlocked
            ? { id: "microphoneAccess", state: "off", status: "Blocked", detail: "Speecher can't open a microphone. Allow it in your system's privacy settings.", fix: "Open microphone settings" }
            : { id: "microphoneAccess", state: "ok", status: "Allowed", detail: "Recording from " + Audio.deviceName + ".", fix: "" },
        _fixing.includes("desktopAccessibility") ? _working("desktopAccessibility")
            : accessibility
            ? { id: "desktopAccessibility", state: "ok", status: "On", detail: "Paste rules, context and learning can see the app you are in.", fix: "" }
            : { id: "desktopAccessibility", state: "off", status: "Off", detail: "Speecher cannot tell which app you are in, so paste rules and context are limited.", fix: "Enable" },
        { id: "globalShortcuts", state: "ok", status: "Registered", detail: "Your desktop delivers the shortcut in every app, including on Wayland.", fix: "" },
        _fixing.includes("virtualKeyboardHealth") ? _working("virtualKeyboardHealth")
            : virtualKeyboard === "running"
            ? { id: "virtualKeyboardHealth", state: "ok", status: "Ready", detail: "Speecher types into other apps directly.", fix: "" }
            : virtualKeyboard === "stopped"
            ? { id: "virtualKeyboardHealth", state: "attention", status: "Stopped", detail: "Set up, but its service isn't running, so Speecher copies to the clipboard.", fix: "Start" }
            : { id: "virtualKeyboardHealth", state: "attention", status: "Not set up", detail: "Speecher can only copy to the clipboard. Setup asks for your password once.", fix: "Set up" }
    ]
    readonly property int checksPassing: checks.filter(c => c.state === "ok").length
    readonly property string checksText: (checksPassing === checks.length ? "All " + checks.length : checksPassing + " of " + checks.length) + " checks pass."

    function check(id) { return checks.find(c => c.id === id) }
    function fix(id) {
        _fixing = _fixing.concat(id)
        _fixTimer.restart()
    }
    // What can be done to a virtual keyboard that is set up, as a menu.
    readonly property var virtualKeyboardActions: [
        { id: "disable", label: "Disable in Speecher", icon: "power-off", help: "Keeps the setup for later." },
        { id: "remove", label: "Remove setup", icon: "trash-2", help: "Asks for administrator permission." }
    ]
    // Stops using the virtual keyboard but keeps its setup, or removes the
    // setup, which asks for administrator permission.
    function disableVirtualKeyboard() { virtualKeyboard = "stopped" }
    function removeVirtualKeyboard() { virtualKeyboard = "notSetUp" }
    function _working(id) { return { id: id, state: "working", status: "Working…", detail: "", fix: "" } }
    property var _fixing: []
    property Timer _fixTimer: Timer {
        interval: Theme.ms(1600)
        onTriggered: {
            if (system._fixing.includes("microphoneAccess")) system._microphoneBlocked = false
            if (system._fixing.includes("desktopAccessibility")) system.accessibility = true
            if (system._fixing.includes("virtualKeyboardHealth")) system.virtualKeyboard = "running"
            system._fixing = []
        }
    }

    // Phone transfer -----------------------------------------------------
    property string computerName: "fox-desktop"
    property string phoneLink: "speecher://import?v=1&a=192.168.1.24:47213&p=8KQ2-M4TZ&n=fox-desktop"
    // "Brings over 7 vocabulary terms, 4 snippets and 6 Writing Profiles."
    readonly property string phoneSummary: "Brings over " + Words.list([
        Words.count(Library.terms.length, "vocabulary term", "vocabulary terms"),
        Words.count(Library.replacements.length, "snippet", "snippets"),
        Words.count(Library.profiles.length, "Writing Profile", "Writing Profiles")]) + "."
    readonly property string phoneNote: "The code works while this page is open, on the same network as " + computerName + "."

    // Things Speecher hands to the desktop ---------------------------------
    // The mock can't open other apps or touch the clipboard, so it says what
    // core would have done.
    property string clipboard: ""
    function copy(text) { clipboard = text }
    function open(what) { announced("Opened " + what + ".", "external-link") }
    // Something done out of sight that the window should mention.
    signal announced(string text, string icon)
    function openShortcutSettings() { open("your system's shortcut settings") }

    // General's wording for the phone code and removing Speecher.
    readonly property var generalText: ({
        showCode: "Show code",
        hideCode: "Hide code",
        remove: "Remove…",
        confirmRemove: "Remove Speecher",
        removeSettings: "Also delete my settings, vocabulary and insights"
    })
    // Shortcuts' wording for what the desktop does with them.
    readonly property var shortcutsText: ({
        whileDictatingCaption: "These only work while a dictation is in progress, so they can reuse keys other apps have.",
        systemSettings: "System settings"
    })

    // Removing Speecher from this computer. Returns what happened, for the
    // page to show.
    property bool installed: true
    function uninstall(keepSettings) {
        installed = false
        return keepSettings ? "Speecher is removed. Your settings stay in ~/.config/speecher."
                            : "Speecher, its settings, vocabulary and insights are removed."
    }

    // Setup assistant ----------------------------------------------------
    // The steps on this platform, in order, as core's setupSteps() gives
    // them. Where each one stands comes from the assistant's choices.
    readonly property var setupSteps: [
        { id: "welcome", title: "Welcome",
          description: "Speecher records a short dictation, turns it into text and sends it to the app you were using. This assistant checks everything dictation needs." },
        { id: "transcription", title: "Transcription",
          description: "Choose the service that turns your speech into text." },
        { id: "microphone", title: "Microphone",
          description: "Choose the microphone Speecher should record, then speak to check that the level moves." },
        { id: "accessibility", title: "Accessibility",
          description: "Speecher pastes into the app you are using and reads the text around your cursor so cleanup understands the context. On Linux both work through the desktop accessibility service, which also lets Speecher learn your corrections." },
        { id: "delivery", title: "Text delivery",
          description: "To type for you, Speecher installs a small virtual keyboard. Your computer asks for your administrator password once; Speecher itself never runs privileged." },
        { id: "refinement", title: "Refinement",
          description: "Refinement cleans up what you said: punctuation, filler words and the formatting each app expects. Choose a provider, or skip cleanup." },
        { id: "shortcut", title: "Shortcut",
          description: "Choose what starts dictation: a key combination, or one key on its own, such as Right Alt. Then choose what pressing it does." },
        { id: "ready", title: "Ready to dictate", description: "" }
    ]
    // The assistant's own wording, around the steps.
    readonly property var setupText: ({
        title: "Set up Speecher",
        runAgain: "You can run setup again any time from General.",
        skip: "Skip setup",
        back: "Back",
        getStarted: "Get started",
        continue_: "Continue",
        startDictating: "Start dictating",
        allPassed: "Every step passed its check.",
        notChosen: "Not chosen",
        welcomeTitle: "Welcome to Speecher",
        welcomeHint: "Press it in any app, speak, and Speecher types what you said.",
        signInsFound: "Sign-ins found on this computer",
        noSignIns: "No sign-ins found yet. The next step helps you choose a service.",
        downloadModel: "Download a model to continue",
        downloadModelHelp: "The download keeps going while you finish setup.",
        clipboardInstead: "Copy to the clipboard instead",
        clipboardInsteadHelp: "Skip the virtual keyboard. Speecher copies your words and you paste them yourself.",
        clipboardOnly: "Clipboard only",
        cancelShortcutHelp: "Optional. Throws away the dictation in progress."
    })
    // Ready's heading over the steps still open: "2 steps need attention".
    function stepsOpenText(count) { return count === 1 ? "One step needs attention" : Words.num(count) + " steps need attention" }
}
