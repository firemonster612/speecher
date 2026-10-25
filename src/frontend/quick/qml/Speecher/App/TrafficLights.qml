pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Speecher.Theme
import Speecher.Controls
import Speecher.Wording

// macOS's close, minimize and zoom buttons for a window that draws its own
// frame: three lights in macOS's own colours, which show what they do while
// the pointer is over any of them.
Row {
    id: lights
    required property Window window

    spacing: Theme.space2

    HoverHandler { id: hover }

    component Light: Rectangle {
        id: light
        required property color tint
        required property string glyph
        required property string label
        signal clicked()

        width: Theme.trafficLight
        height: width
        radius: width / 2
        color: tap.pressed ? Qt.darker(tint, 1.2) : tint
        border.width: 1
        border.color: Qt.darker(tint, 1.15)
        Accessible.role: Accessible.Button
        Accessible.name: label
        Accessible.onPressAction: clicked()

        Icon {
            anchors.centerIn: parent
            visible: hover.hovered
            name: light.glyph
            size: 8
            color: Qt.rgba(0, 0, 0, 0.55)
        }
        TapHandler { id: tap; onTapped: light.clicked() }
    }

    Light { tint: "#ff5f57"; glyph: "x"; label: Words.shell.close; onClicked: lights.window.close() }
    Light { tint: "#febc2e"; glyph: "minus"; label: Words.shell.minimize; onClicked: lights.window.showMinimized() }
    Light {
        tint: "#28c840"
        glyph: "plus"
        label: Words.shell.zoom
        onClicked: lights.window.visibility === Window.Maximized ? lights.window.showNormal() : lights.window.showMaximized()
    }
}
