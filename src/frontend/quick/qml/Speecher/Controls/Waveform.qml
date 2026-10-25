pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// Live input level as rounded bars in the signal red, mirrored about the
// middle. Feed level (0 to 1) as audio arrives; the bars scroll left, louder
// ones stronger. It stops ticking once inactive bars have drained to silence.
//   fitWidth: size the bars to fill the width instead of setting barWidth.
//   compress: show the square root of the level, so soft speech still shows
//             and a shout doesn't clip, and let silence drop to a dot.
//   threshold: a level (0 to 1) below which sound counts as quiet: a band
//             marks it and bars inside it turn grey. thresholdText labels
//             the band's edge from beside the bars, clear of loud ones.
//   fadeOlder: bars fade toward the left edge as they age.
Item {
    id: wave

    property real level: 0
    property int bars: 28
    property real barWidth: 3
    property real gap: 2.5
    property bool active: true
    property color color: Theme.negative
    property int interval: Theme.ms(55)
    property bool fitWidth: false
    property bool compress: false
    property real threshold: -1
    property string thresholdText
    property bool fadeOlder: false

    readonly property bool _labelled: threshold >= 0 && thresholdText.length > 0
    readonly property real _barsWidth: width - (_labelled ? label.implicitWidth + Theme.space3 : 0)
    readonly property real _barWidth: fitWidth ? Math.max(2, (_barsWidth - gap * (bars - 1)) / bars) : barWidth
    function _shown(v) { return compress ? Math.sqrt(Math.max(0, Math.min(1, v))) : 0.12 + 0.88 * v }

    implicitWidth: bars * (barWidth + gap) - gap
    implicitHeight: 28

    property var _levels: Array(bars).fill(0)

    Timer {
        interval: wave.interval
        repeat: true
        running: wave.visible && (wave.active || wave._levels.some(v => v > 0.001))
        onTriggered: {
            // Keeps exactly one value per bar, oldest first, even after the
            // bar count changes with the width.
            const next = wave._levels.slice(-(wave.bars - 1))
            while (next.length < wave.bars - 1) next.unshift(0)
            next.push(wave.active ? wave.level : 0)
            wave._levels = next
        }
    }

    // The quiet band.
    Rectangle {
        id: band
        visible: wave.threshold >= 0
        width: wave._barsWidth
        height: Math.max(2, wave._shown(wave.threshold) * wave.height)
        y: (wave.height - height) / 2
        radius: Theme.radiusSmall
        color: Theme.fill
        Behavior on height { Ease {} }
    }
    Txt {
        id: label
        visible: wave._labelled
        anchors.right: parent.right
        y: band.y - height / 2
        text: wave.thresholdText
        variant: "caption"
        tone: "tertiary"
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        spacing: wave.gap
        Repeater {
            model: wave.bars
            Rectangle {
                required property int index
                readonly property real v: wave._levels[index] || 0
                readonly property bool quiet: wave.threshold >= 0 && v < wave.threshold
                readonly property bool silent: wave.compress && v < 0.002

                width: wave._barWidth
                height: Math.max(wave._barWidth, wave.height * wave._shown(v))
                radius: width / 2
                anchors.verticalCenter: parent.verticalCenter
                color: silent ? Theme.rule : quiet ? Theme.textTertiary
                     : Qt.rgba(wave.color.r, wave.color.g, wave.color.b, wave.active ? 0.35 + 0.65 * Math.min(1, v * 1.6) : 0.3)
                opacity: wave.fadeOlder && !silent ? 0.3 + 0.7 * index / Math.max(1, wave.bars - 1) : 1
                Behavior on height { Ease { duration: wave.interval * 1.6 } }
                Behavior on color { EaseColor {} }
            }
        }
    }
}
