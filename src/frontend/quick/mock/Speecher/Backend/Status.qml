pragma Singleton
import QtQuick

// The preview's app-wide state the shell shows: the version and the
// shortcut, read from the mock settings. The app has a C++ Status with the
// same members.
QtObject {
    readonly property var shortcutKeys: Settings.values.globalShortcut || []
    readonly property string version: "0.9.3"
}
