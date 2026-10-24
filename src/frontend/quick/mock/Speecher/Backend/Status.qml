pragma Singleton
import QtQuick

// Mock of the app-wide state the shell shows: the version, the shortcut,
// and a pending update, read from the settings and the updater, and the
// shell's wording: the window, the sidebar and the search palette.
QtObject {
    readonly property var shortcutKeys: Settings.values.globalShortcut || []
    readonly property string version: Updates.currentVersion
    readonly property bool updateReady: Updates.state === "ready"
    readonly property string updateVersion: Updates.availableVersion

    readonly property var text: ({
        appName: "Speecher",
        version: "Version " + version,
        searchSettings: "Search settings",
        updateReady: "Restart to update to " + updateVersion,
        searchPlaceholder: "Search settings and pages",
        suggested: "Suggested",
        page: "Page",
        searchTip: "Try a setting's name, a service such as Claude, or a word like hotkey.",
        moveHint: "Move",
        openHint: "Open",
        closeHint: "Close",
        minimize: "Minimize",
        maximize: "Maximize",
        restore: "Restore",
        close: "Close"
    })
    // Wording that takes a value is a method, as a C++ QObject would give it.
    function noMatches(query) { return "Nothing matches “" + query + "”" }
    function results(count) { return Words.count(count, "result", "results") }
}
