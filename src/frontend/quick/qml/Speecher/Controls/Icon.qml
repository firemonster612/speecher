pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Speecher.Theme
import "icons.js" as Icons
import "brands.js" as Brands

// A Lucide icon by name, drawn from the bundled icon font, or a company's
// mark (brands.js: openai, claude) drawn as a shape, so either takes any
// colour and animates like text.
Item {
    id: icon
    property string name
    property int size: 16
    property color color: Theme.textSecondary

    readonly property string _brand: Brands.paths[name] || ""

    width: size
    height: size
    Accessible.ignored: true

    Behavior on color { EaseColor {} }

    Text {
        visible: !icon._brand
        anchors.fill: parent
        color: icon.color
        text: Icons.codes[icon.name] !== undefined ? String.fromCodePoint(Icons.codes[icon.name]) : ""
        font.family: Theme.iconFamily
        font.pixelSize: icon.size
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    // A mark fills its whole box where a Lucide glyph keeps a margin, so it
    // is drawn a little smaller to sit at the same weight.
    Loader {
        active: !!icon._brand
        anchors.centerIn: parent
        sourceComponent: Shape {
            width: 24
            height: 24
            scale: icon.size * 0.86 / 24
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: icon.color
                strokeColor: "transparent"
                fillRule: ShapePath.WindingFill
                PathSvg { path: icon._brand }
            }
        }
    }
}
