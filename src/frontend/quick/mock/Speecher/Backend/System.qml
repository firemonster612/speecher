pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Mock of the app's System (host/QuickSystem): the health checks and their
// fixes, the virtual keyboard's setup and typing test, the clipboard and
// opening links, sending settings to the phone, and removing Speecher. Like
// the app's, it carries states; the UI words them (Health, the pages).
//
// --health=issues fails three of the checks. --accessibility=off and
// --virtual-keyboard=disabled|daemonNotRunning|notInstalled start one so.
QtObject {
    id: system

    readonly property string _health: Launch.arg("health", "default")

    // Permissions and health --------------------------------------------
    property bool accessibility: _health !== "issues" && Launch.arg("accessibility", "on") !== "off"
    // The virtual keyboard's reason, as the app's check gives it.
    property string _keyboard: _health === "issues" ? "notInstalled" : Launch.arg("virtual-keyboard", "ready")
    readonly property var _keyboardFixes: ({ disabled: "enable", notInstalled: "setUp", daemonNotRunning: "start" })

    // One entry per health row: {id, state, reason, fix}.
    readonly property var checks: [
        { id: "microphoneAccess", state: "ok", reason: "notAsked", fix: "" },
        _fixing.includes("desktopAccessibility") ? _working("desktopAccessibility", "on")
            : accessibility ? { id: "desktopAccessibility", state: "ok", reason: "on", fix: "" }
            : { id: "desktopAccessibility", state: "off", reason: "off", fix: "enable" },
        { id: "globalShortcuts", state: "ok", reason: "registered", fix: "" },
        _fixing.includes("virtualKeyboardHealth") ? _working("virtualKeyboardHealth", "settingUp")
            : virtualKeyboardTest !== "off" ? { id: "virtualKeyboardHealth", state: "attention", reason: "testing", fix: "" }
            : _keyboard === "ready" ? { id: "virtualKeyboardHealth", state: "ok", reason: "ready", fix: "" }
            : { id: "virtualKeyboardHealth", state: "attention", reason: _keyboard, fix: _keyboardFixes[_keyboard] || "" }
    ]
    function check(id) { return checks.find(c => c.id === id) }
    function fix(id) {
        if (id === "virtualKeyboardHealth" && _keyboard === "disabled") {
            virtualKeyboardTest = "ready"
            return
        }
        _fixing = _fixing.concat(id)
        _fixTimer.restart()
    }
    readonly property var virtualKeyboardActions: _keyboard === "ready" ? ["disable", "remove"]
                                                 : _keyboard === "notInstalled" ? [] : ["remove"]
    function disableVirtualKeyboard() { _keyboard = "disabled" }
    function removeVirtualKeyboard() { _keyboard = "notInstalled" }

    // The typing test: off, ready, typing or missed. The mock's keyboard
    // types nothing, so a run misses.
    property string virtualKeyboardTest: Launch.arg("keyboard-test", "off")
    function runVirtualKeyboardTest() { virtualKeyboardTest = "typing"; _typingTimer.restart() }
    function virtualKeyboardTestReceived(text) {}
    function cancelVirtualKeyboardTest() { virtualKeyboardTest = "off" }

    function _working(id, reason) { return { id: id, state: "working", reason: reason, fix: "" } }
    property var _fixing: []
    property Timer _fixTimer: Timer {
        interval: Theme.ms(1600)
        onTriggered: {
            if (system._fixing.includes("desktopAccessibility")) system.accessibility = true
            if (system._fixing.includes("virtualKeyboardHealth")) system.virtualKeyboardTest = "ready"
            system._fixing = []
        }
    }
    property Timer _typingTimer: Timer {
        interval: Theme.ms(1500)
        onTriggered: system.virtualKeyboardTest = "missed"
    }

    // Phone transfer -----------------------------------------------------
    // The preview has no image://qr provider, so its code stays blank.
    readonly property string computerName: "fox-desktop"
    property string phoneTransfer: ""
    readonly property string phoneLink: ""
    readonly property var phoneBundle: ({ terms: Library.terms.length, replacements: Library.replacements.length,
                                          profiles: Library.profiles.length })
    function startPhoneTransfer() { phoneTransfer = "waiting" }
    function stopPhoneTransfer() { phoneTransfer = "" }

    // Things Speecher hands to the desktop ---------------------------------
    // The mock can't open other apps or touch the clipboard, so it says what
    // the app would have done.
    function copy(text) {}
    function open(what) { announced("Opened " + what + ".", "external-link") }
    // Something done out of sight that the window should mention.
    signal announced(string text, string icon)
    readonly property bool canOpenShortcutSettings: true
    function openShortcutSettings() { open("your system's shortcut settings") }

    // Removing Speecher from this computer -------------------------------
    property bool removing: false
    property var removal: ({})
    function uninstall(keepSettings) {
        removal = { removed: ["app menu entry", "speecher command", "icon", "Global Shortcut registration"].concat(keepSettings ? [] : ["settings"]),
                    problems: [], settingsDeleted: !keepSettings, programFile: "/home/fox/Applications/Speecher.AppImage" }
    }
    function quit() { announced("Speecher would quit now.", "power-off") }
}
