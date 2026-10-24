import QtQuick
import Speecher.Theme

// A made-up voice level, 0 to 1, for illustrations, previews and the mock
// backend: syllables come in bursts, with short pauses between words and a
// longer breath now and then. It drops to silence when running stops.
QtObject {
    id: voice

    property bool running: true
    property real level: 0
    property real _phase: 0

    readonly property Timer _tick: Timer {
        interval: Theme.ms(45)
        repeat: true
        running: voice.running
        onRunningChanged: if (!running) voice.level = 0
        onTriggered: {
            voice._phase += 0.045
            const t = voice._phase
            const word = Math.max(0, Math.sin(t * 2.3) + 0.35 * Math.sin(t * 0.7))
            const syllable = 0.55 + 0.45 * Math.abs(Math.sin(t * 9.5))
            const breath = Math.sin(t * 0.37) > 0.82 ? 0.15 : 1
            const noise = 0.006 + Math.random() * 0.01
            voice.level = Math.min(1, noise + 0.42 * word * syllable * breath * (0.75 + Math.random() * 0.5))
        }
    }
}
