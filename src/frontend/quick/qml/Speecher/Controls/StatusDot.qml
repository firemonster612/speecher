import QtQuick
import Speecher.Theme

// A status dot. pulse sends a soft ring outward from it on a loop.
Item {
    id: root
    property color color: Theme.positive
    property bool pulse: false
    property int size: 8

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        id: halo
        anchors.centerIn: parent
        width: root.size
        height: root.size
        radius: width / 2
        color: root.color
        opacity: 0
        SequentialAnimation on scale {
            running: root.pulse && root.visible
            loops: Animation.Infinite
            Ease { from: 1; to: Theme.motionScale(2.6); duration: Theme.ms(1600) }
            PauseAnimation { duration: Theme.ms(400) }
        }
        SequentialAnimation on opacity {
            running: root.pulse && root.visible
            loops: Animation.Infinite
            Ease { from: 0.45; to: 0; duration: Theme.ms(1600) }
            PauseAnimation { duration: Theme.ms(400) }
        }
    }
    Rectangle { anchors.fill: parent; radius: width / 2; color: root.color }
}
