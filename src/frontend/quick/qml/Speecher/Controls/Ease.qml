import QtQuick
import Speecher.Theme

// A number easing on a Theme curve over a Theme duration. Use it in every
// Behavior and transition instead of a bare NumberAnimation, which falls back
// to Qt's linear default:
//   Behavior on opacity { Ease {} }
//   Behavior on x { Ease { duration: Theme.slow; curve: Theme.springy } }
NumberAnimation {
    property var curve: Theme.easeOut
    duration: Theme.base
    easing.type: Easing.BezierSpline
    easing.bezierCurve: curve
}
