pragma Singleton
import QtQuick

// The preview's app-wide state the shell shows: the version, the shortcut,
// and a pending update, read from the mock settings and updater. The app has
// a C++ Status with the same members.
QtObject {
    readonly property var shortcutKeys: Settings.values.globalShortcut || []
    readonly property string version: Updates.currentVersion
    readonly property bool updateReady: Updates.state === "ready"
    readonly property string updateVersion: Updates.availableVersion
}
