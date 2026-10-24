import QtQuick
import Speecher.Theme

// A progress bar. value runs 0 to 1; indeterminate sweeps a short segment
// across the track.
Item {
    id: bar
    property real value: 0
    property bool indeterminate: false
    property color color: Theme.accent

    implicitWidth: 200
    implicitHeight: 6

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: Theme.fillStrong
        clip: true

        Rectangle {
            visible: !bar.indeterminate
            height: parent.height
            width: Math.max(height, parent.width * Math.max(0, Math.min(1, bar.value)))
            radius: height / 2
            color: bar.color
            Behavior on width { Ease { duration: Theme.slow } }
        }
        Rectangle {
            id: sweep
            visible: bar.indeterminate
            height: parent.height
            width: parent.width * 0.3
            radius: height / 2
            color: bar.color
            Ease on x {
                running: bar.indeterminate && bar.visible
                from: -sweep.width; to: track.width
                duration: Theme.ms(1300); loops: Animation.Infinite
                curve: Theme.steady
            }
        }
    }
}
