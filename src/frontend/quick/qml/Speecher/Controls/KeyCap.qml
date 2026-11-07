import QtQuick
import Speecher.Theme

// One keyboard key, drawn as a raised cap. size: small, medium, large, as
// Button's. pressed sinks it into its base. The arrows and Return are drawn
// as icons, since the bundled font has no glyph for them and Windows' falls
// back to boxes.
Item {
    id: cap
    property string text
    property string size: "medium"
    property bool pressed: false
    property bool lit: false

    readonly property bool _large: size === "large"
    readonly property bool _small: size === "small"
    readonly property int _depth: _large ? 4 : 2
    readonly property int _radius: _large ? Theme.radiusLarge : _small ? Theme.radiusSmall : Theme.radius
    readonly property string _icon: ({ "↑": "arrow-up", "↓": "arrow-down", "←": "arrow-left", "→": "arrow-right",
                                       "↵": "corner-down-left" })[text] || ""

    implicitWidth: Math.max(implicitHeight, (_icon ? glyph.width : label.implicitWidth) + (_large ? 34 : _small ? 10 : 14))
    implicitHeight: _large ? 54 : _small ? 20 : 24

    Rectangle {
        anchors.fill: parent
        radius: cap._radius
        color: cap.lit ? Theme.keyBaseLit : Theme.keyBase
        Behavior on color { EaseColor { duration: Theme.base } }
    }
    Rectangle {
        width: parent.width
        height: parent.height - cap._depth
        y: cap.pressed ? cap._depth - 1 : 0
        radius: cap._radius
        color: cap.lit ? Theme.text : Theme.raised
        border.width: 1
        border.color: cap.lit ? Theme.text : Theme.border
        Behavior on y { enabled: Theme.movement > 0; Ease { duration: Theme.fast } }
        Behavior on color { EaseColor { duration: Theme.base } }

        Icon {
            id: glyph
            visible: cap._icon.length > 0
            anchors.centerIn: parent
            name: cap._icon
            size: cap._large ? 22 : cap._small ? 11 : 13
            color: label.color
        }
        Txt {
            id: label
            visible: !glyph.visible
            anchors.centerIn: parent
            text: cap.text
            variant: cap._large ? "title" : cap._small ? "caption" : "small"
            font.weight: Font.DemiBold
            color: cap.lit ? Theme.sheet : Theme.text
        }
    }
}
