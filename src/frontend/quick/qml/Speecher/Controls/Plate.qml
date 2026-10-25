import QtQuick
import Speecher.Theme

// The recessed panel a page or setup step draws its picture on: the
// shortcut, the level meter, the marked-up sample, the paste route, the
// popup preview. Content lays itself out inside.
Rectangle {
    width: parent ? parent.width : implicitWidth
    radius: Theme.radiusLarge
    color: Theme.surface
}
