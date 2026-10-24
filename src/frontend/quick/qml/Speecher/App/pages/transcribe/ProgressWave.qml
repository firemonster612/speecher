pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// How far the current file is: its waveform as bars, the part already heard
// in ink and the rest faint, with a pencil playhead between them.
Item {
    id: wave

    property real progress: 0
    // The file's peak levels, 0..1 across its length; flat until it is read.
    property var peaks: []
    readonly property real barWidth: 2
    readonly property real gap: 2
    readonly property int bars: Math.max(1, Math.floor((width + gap) / (barWidth + gap)))
    // The loudest peak under each bar, scaled to the file's loudest.
    readonly property var levels: {
        const loudest = Math.max(0.01, ...peaks)
        const out = []
        for (let i = 0; i < bars; ++i) {
            const from = Math.floor(i * peaks.length / bars)
            const to = Math.max(from + 1, Math.floor((i + 1) * peaks.length / bars))
            out.push(Math.max(0.08, Math.max(0, ...peaks.slice(from, to)) / loudest))
        }
        return out
    }
    property real _shown: progress
    Behavior on _shown { Ease { duration: Theme.slow } }

    implicitHeight: 56
    Accessible.role: Accessible.ProgressBar

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: wave.gap
        Repeater {
            model: wave.bars
            Rectangle {
                required property int index
                readonly property bool heard: index / Math.max(1, wave.bars - 1) <= wave._shown
                width: wave.barWidth
                height: Math.max(2, wave.height * wave.levels[index])
                radius: 1
                anchors.verticalCenter: parent.verticalCenter
                color: heard ? Theme.text : Theme.rule
                Behavior on color { EaseColor { duration: Theme.base } }
            }
        }
    }

    // The playhead.
    Rectangle {
        x: wave.width * wave._shown - 1
        y: -4
        width: 2
        height: parent.height + 8
        radius: 1
        color: Theme.accent
    }
}
