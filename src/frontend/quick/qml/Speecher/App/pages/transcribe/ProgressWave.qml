pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// How far the current file is: its waveform as bars, the part already heard
// in ink and the rest faint, with a pencil playhead between them.
Item {
    id: wave

    property real progress: 0
    property int seed: 1
    readonly property real barWidth: 2
    readonly property real gap: 2
    readonly property int bars: Math.max(1, Math.floor((width + gap) / (barWidth + gap)))
    // A made-up but steady shape per file: speech-like bursts with pauses.
    readonly property var levels: {
        let s = seed * 7919
        const next = () => { s = (s * 1664525 + 1013904223) % 4294967296; return s / 4294967296 }
        const out = []
        for (let i = 0; i < bars; ++i) {
            const phrase = 0.55 + 0.45 * Math.sin(i / 9 + seed)
            out.push(next() < 0.06 ? 0.08 : Math.max(0.12, Math.min(1, phrase * (0.35 + next() * 0.75))))
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
