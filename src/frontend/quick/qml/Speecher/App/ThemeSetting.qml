import QtQuick
import Speecher.Theme
import Speecher.Backend

// Makes Theme follow the Theme setting: dark or light as chosen, or the
// desktop's scheme on System. The host creates one before any window, so
// every window it opens, the setup assistant and the popup included, follows.
Binding {
    target: Theme
    property: "dark"
    value: Settings.values.themeControl === "dark" || (Settings.values.themeControl !== "light" && Theme.systemDark)
}
