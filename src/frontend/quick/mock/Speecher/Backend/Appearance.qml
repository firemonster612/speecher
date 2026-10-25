pragma Singleton
import QtQuick
import Speecher.Launch
import Speecher.Theme

// The preview's Appearance: the colours a Plasma desktop gives, Breeze or
// Breeze Dark as the desktop's theme is, two installed schemes, and a blur
// the preview cannot draw. --blur=false is a desktop that has none. The app
// has a C++ Appearance with the same members.
QtObject {
    readonly property var _breeze: ({ window: "#EFF0F1", text: "#232629", accent: "#3DAEE9", negative: "#DA4453" })
    readonly property var _breezeDark: ({ window: "#202326", text: "#FCFCFC", accent: "#3DAEE9", negative: "#DA4453" })

    readonly property var systemColors: Theme.systemDark ? _breezeDark : _breeze
    readonly property var installedSchemes: [
        { id: "BreezeDark", name: "Breeze Dark", colors: _breezeDark },
        { id: "Honeycomb", name: "Honeycomb", colors: { window: "#F4EBD6", text: "#3A2F1C", accent: "#D98E04", negative: "#C0392B" } }
    ]
    readonly property bool canBlur: Launch.arg("blur", "true") === "true"

    function setBlurBehind(window, area, radius, dark) {}
}
