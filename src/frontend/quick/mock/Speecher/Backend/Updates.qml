pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// The preview's updater, with the members of the app's C++ Updates
// (host/QuickUpdates.h documents them): where it is in a check, download or
// restart, and What's New. check(), updateNow() and installAndRestart() walk
// through the states with fake delays.
//
// --update-state=<state> starts in that state; --whats-new-pending=true
// starts with news not yet seen.
QtObject {
    id: updates

    property string state: Launch.arg("update-state", "ready")
    property string availableVersion: "0.9.4"
    property real progress: state === "downloading" ? 0.42 : 0
    property string error: state === "error" ? "Could not download the AppImage: Connection closed."
                         : state === "checkFailed" ? "Could not check for updates: Host not found." : ""
    readonly property bool automaticDownloads: true
    property bool manualInstall: false
    property bool stableReplacement: false
    property string restartBlocker: ""
    property date lastChecked: new Date(Date.now() - 4 * 60 * 1000)
    property bool _putOff: false
    readonly property bool attention: !["idle", "checking", "upToDate"].includes(state)
                                      && !(_putOff && (state === "ready" || state === "restartPending"))
    // Whether the preview pretends an update from _lastSeen was installed.
    readonly property bool _updated: Launch.arg("whats-new-pending", "false") === "true"
    property bool whatsNewPending: _updated
    readonly property var whatsNew: ({
        commits: [],
        releases: [
            { version: "0.9.3", sections: [
                { title: "Added", items: [
                    "CLI Proxy API accounts work end to end: setup recognises them, and you can pick the account without leaving setup.",
                    "An opt-in accuracy pass retranscribes the whole recording when you stop, catching mistakes the live stream made.",
                    "Hesitation self-corrections apply: \"meet at five, no, six\" delivers six."
                ] },
                { title: "Changed", items: [
                    "Setting a shortcut is one control: click, press your combination or a single key, done."
                ] },
                { title: "Fixed", items: [
                    "Dictations no longer lose text when a speech stream drops mid-sentence."
                ] }
            ] }
        ]
    })
    // The version run before the update the preview pretends happened.
    readonly property string _lastSeen: "0.2.0"

    signal pageRequested(string pageId)

    function check() { _go("checking", "available", 1400) }
    function updateNow() {
        if (state === "available") { state = "downloading"; progress = 0; _download.restart() }
        else if (state === "ready") state = "restarting"
        else if (state === "error" || state === "checkFailed") check()
    }
    function installAndRestart() { _restartWhenReady = true; updateNow() }
    function later() { _putOff = true }
    function dismiss() { state = "idle" }
    function openAllReleases() { System.open("the releases page in your browser") }
    function newSince(version) { return _updated && version > _lastSeen }
    function acknowledgeWhatsNew() { whatsNewPending = false }
    function showWhatsNew() { pageRequested("updates") }

    property bool _restartWhenReady: false
    property string _next: ""
    function _go(now, next, ms) { state = now; _next = next; _step.interval = Theme.ms(ms); _step.restart() }
    property Timer _step: Timer { onTriggered: updates.state = updates._next }
    property Timer _download: Timer {
        interval: Theme.ms(120)
        repeat: true
        onTriggered: {
            updates.progress = Math.min(1, updates.progress + 0.035 + Math.random() * 0.04)
            if (updates.progress < 1) return
            stop()
            updates.state = updates._restartWhenReady ? "restarting" : "ready"
        }
    }
}
