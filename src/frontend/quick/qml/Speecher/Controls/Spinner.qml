import QtQuick
import QtQuick.Shapes
import Speecher.Theme

// An indeterminate busy indicator: a spinning arc over a faint ring.
Item {
    id: root
    property int size: 16
    property color color: Theme.textSecondary
    property real lineWidth: Math.max(1.5, size / 9)

    implicitWidth: size
    implicitHeight: size

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.2)
            strokeWidth: root.lineWidth
            fillColor: "transparent"
            PathAngleArc { centerX: root.size / 2; centerY: root.size / 2; radiusX: (root.size - root.lineWidth) / 2; radiusY: radiusX; startAngle: 0; sweepAngle: 360 }
        }
    }
    Shape {
        id: arc
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.lineWidth
            capStyle: ShapePath.RoundCap
            fillColor: "transparent"
            PathAngleArc { centerX: root.size / 2; centerY: root.size / 2; radiusX: (root.size - root.lineWidth) / 2; radiusY: radiusX; startAngle: -90; sweepAngle: 100 }
        }
        RotationAnimator on rotation { from: 0; to: 360; duration: Theme.ms(800); loops: Animation.Infinite; running: root.visible }
    }
}
