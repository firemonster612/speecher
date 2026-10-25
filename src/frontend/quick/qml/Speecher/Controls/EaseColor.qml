import QtQuick
import Speecher.Theme

// Ease for colours: Behavior on color { EaseColor {} }.
ColorAnimation {
    property var curve: Theme.easeOut
    duration: Theme.fast
    easing.type: Easing.BezierSpline
    easing.bezierCurve: curve
}
