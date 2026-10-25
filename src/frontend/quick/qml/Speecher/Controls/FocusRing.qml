import QtQuick
import Speecher.Theme

// The keyboard focus ring: a pencil outline that grows in around target,
// following its corner radius.
Rectangle {
    property Item target
    property bool shown: false
    property real targetRadius: (target as Rectangle)?.radius ?? Theme.radius

    anchors.fill: target
    anchors.margins: shown ? -3 : 0
    radius: targetRadius - anchors.margins
    color: "transparent"
    border.width: 2
    border.color: Theme.accentRing
    opacity: shown ? 1 : 0
    Behavior on opacity { Ease { duration: Theme.fast } }
    Behavior on anchors.margins { enabled: Theme.movement > 0; Ease {} }
}
