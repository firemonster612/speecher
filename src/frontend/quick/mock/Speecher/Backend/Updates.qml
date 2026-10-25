pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// Placeholder: the app ships this mock until its wiring PR replaces it.
// Mock of the updater: where it is in a check or install, and the release
// notes for the running version. check(), install() and restart() walk
// through the states with fake delays.
//
// state: idle, checking, upToDate, available, downloading, ready, error.
// --update-state=<state> starts in that state.
QtObject {
    id: updates

    property string state: Launch.arg("update-state", "ready")
    property string currentVersion: "0.9.3"
    property string releaseDate: "October 14, 2026"
    property string availableVersion: "0.9.4"
    property string availableSize: "38 MB"
    property real progress: state === "downloading" ? 0.42 : 0
    property string lastChecked: "Checked 4 minutes ago"
    property string error: "Couldn't reach the update server."
    // Installing replaces the AppImage in place; elsewhere it opens a page.
    property bool automaticDownloads: true

    // What the update would bring, shown before installing.
    property var availableHighlights: [
        "Faster first words with Claude Voice",
        "Vocabulary terms sync to your phone",
        "Fixes a crash when the target app closes mid-dictation"
    ]

    // Release notes for the running version, already filtered to this
    // platform, in groups with core's heading for each.
    property var releaseNotes: [
        { title: "Added", items: [
            "CLI Proxy API accounts work end to end: setup recognises them, and you can pick the account without leaving setup.",
            "An opt-in accuracy pass retranscribes the whole recording when you stop, catching mistakes the live stream made.",
            "Hesitation self-corrections apply: \"meet at five, no, six\" delivers six.",
            "Push to talk on desktops that report key release, including Plasma and GNOME 48."
        ] },
        { title: "Changed", items: [
            "Setting a shortcut is one control: click, press your combination or a single key, done.",
            "The setup assistant checks what each step set up and holds Next until it works."
        ] },
        { title: "Fixed", items: [
            "Dictations no longer lose text when a speech stream drops mid-sentence.",
            "Dictated lists keep the numbers you spoke instead of renumbering from 1."
        ] }
    ]

    // Settings added since the version you last saw, by row id.
    property var newSettings: ["pauseShortcut", "cancelShortcut", "updateCheckInterval"]

    // Wording -------------------------------------------------------------
    readonly property var text: ({
        checkNow: "Check now",
        upToDate: "Speecher is up to date",
        releaseNotes: "Release notes",
        updateNow: "Update now",
        openReleasePage: "Open release page",
        downloadingDetail: "Keep dictating; it installs when you restart.",
        readyTitle: "Restart to finish updating",
        later: "Later",
        restartNow: "Restart now",
        checkFailed: "Update check failed",
        tryAgain: "Try again",
        allReleases: "All releases",
        newSettings: "New settings",
        newSettingsCaption: "Added since the version you used before."
    })
    readonly property string currentTitle: "Speecher " + currentVersion
    readonly property string whatsNewTitle: "What's new in " + currentVersion
    readonly property string availableTitle: "Speecher " + availableVersion + " is available"
    readonly property string downloadingTitle: "Downloading Speecher " + availableVersion
    readonly property string readyDetail: "Speecher " + availableVersion + " is downloaded and ready."
    // "Stable channel, released October 14, 2026.", or without a channel
    // when none is chosen.
    function releasedText(channel) { return (channel ? channel + " channel, released " : "Released ") + releaseDate + "." }
    function checkingText(channel) { return "Checking the " + channel + " channel…" }

    function check() {
        state = "checking"
        _next = "available"
        _step.interval = Theme.ms(1400)
        _step.restart()
    }
    function install() {
        state = "downloading"
        progress = 0
        _download.restart()
    }
    function restart() { state = "upToDate"; currentVersion = availableVersion; lastChecked = "Checked just now" }
    function later() { state = "idle" }
    function openAllReleases() { System.open("the releases page in your browser") }
    function openReleaseNotes() { System.open("the release notes for " + availableVersion + " in your browser") }

    property string _next: ""
    property Timer _step: Timer { onTriggered: updates.state = updates._next }
    property Timer _download: Timer {
        interval: Theme.ms(120)
        repeat: true
        onTriggered: {
            updates.progress = Math.min(1, updates.progress + 0.035 + Math.random() * 0.04)
            if (updates.progress >= 1) { stop(); updates.state = "ready" }
        }
    }
}
