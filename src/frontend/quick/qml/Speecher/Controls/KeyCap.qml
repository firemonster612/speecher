import QtQuick
import Speecher.Theme

// One keyboard key, drawn as a raised cap. size: small, medium, large, as
// Button's. pressed sinks it into its base.
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

    implicitWidth: Math.max(implicitHeight, label.implicitWidth + (_large ? 34 : _small ? 10 : 14))
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

        Txt {
            id: label
            anchors.centerIn: parent
            text: cap.text
            variant: cap._large ? "title" : cap._small ? "caption" : "small"
            font.weight: Font.DemiBold
            color: cap.lit ? Theme.sheet : Theme.text
        }
    }
}
