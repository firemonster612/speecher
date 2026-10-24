import QtQuick
import Speecher.Theme
import Speecher.Backend

// Makes Theme follow the Appearance settings: dark or light as chosen, or the
// desktop's scheme on System; the chosen colours; the layout; and
// translucency where the platform can blur behind a window. The host creates
// one before any window, so every window it opens, the setup assistant and
// the popup included, follows.
QtObject {
    readonly property Binding dark: Binding {
        target: Theme
        property: "dark"
        value: Settings.values.themeControl === "dark" || (Settings.values.themeControl !== "light" && Theme.systemDark)
    }
    readonly property Binding colors: Binding {
        target: Theme
        property: "colorSource"
        value: ColorSchemes.source(Settings.values.colorScheme)
    }
    readonly property Binding split: Binding {
        target: Theme
        property: "split"
        value: Settings.values.windowLayout === "split"
    }
    readonly property Binding translucent: Binding {
        target: Theme
        property: "translucent"
        value: Settings.values.translucency === true && Appearance.canBlur
    }
}
