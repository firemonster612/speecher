import QtQuick
import Speecher.Theme

// The theme as three small windows in the current colours: System (half
// light, half dark), Light and Dark.
LookPicker {
    width: 408
    thumbnail: Item {
        id: thumb
        property var option
        MiniWindow {
            anchors.fill: parent
            scheme: Theme.schemeOf(Theme.colorSource, thumb.option.id === "dark")
        }
        // System splits the window down the middle.
        Item {
            visible: thumb.option.id === "system"
            x: parent.width / 2
            width: parent.width / 2
            height: parent.height
            clip: true
            MiniWindow {
                x: -parent.x
                width: thumb.width
                height: thumb.height
                scheme: Theme.schemeOf(Theme.colorSource, true)
            }
        }
    }
}
