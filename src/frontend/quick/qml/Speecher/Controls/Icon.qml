import QtQuick
import Speecher.Theme
import "icons.js" as Icons

// A Lucide icon by name, drawn from the bundled icon font so it takes any
// colour and animates like text.
Text {
    property string name
    property int size: 16

    width: size
    height: size
    color: Theme.textSecondary
    text: Icons.codes[name] !== undefined ? String.fromCodePoint(Icons.codes[name]) : ""
    font.family: Theme.iconFamily
    font.pixelSize: size
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    Accessible.ignored: true

    Behavior on color { EaseColor {} }
}
