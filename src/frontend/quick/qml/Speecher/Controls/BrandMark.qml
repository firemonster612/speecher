import QtQuick
import Speecher.Theme

// Speecher's mark: the sound-lines glyph in paper on an ink tile.
Rectangle {
    implicitWidth: 28
    implicitHeight: 28
    radius: 7
    color: Theme.text
    Icon { anchors.centerIn: parent; name: "audio-lines"; size: 16; color: Theme.paper }
}
