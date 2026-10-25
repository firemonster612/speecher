pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// Dictations by hour of day: 24 small bars in faint ink, the peak hour and
// the hovered one in pencil. Bars grow and shrink to a new period's counts.
Item {
    id: chart

    property var counts: []
    property int peak: 0

    readonly property real gap: 3
    readonly property real pitch: (width + gap) / 24
    readonly property real barWidth: pitch - gap
    readonly property int plotHeight: 64
    readonly property int maxCount: Math.max(1, ...counts)

    function barHeight(count) { return Math.max(count ? 3 : 1, (plotHeight - 4) * count / maxCount) }

    implicitHeight: plotHeight + 22
    Accessible.role: Accessible.Chart

    Rectangle { y: chart.plotHeight; width: parent.width; height: 1; color: Theme.rule }

    Repeater {
        model: 24
        Rectangle {
            required property int index
            readonly property int count: chart.counts[index] || 0
            readonly property bool lit: index === chart.peak || index === hover.index
            x: index * chart.pitch
            width: chart.barWidth
            height: chart.barHeight(count)
            y: chart.plotHeight - height
            radius: 1
            color: lit ? Theme.accent : count ? Theme.soft(Theme.text, 0.24) : Theme.rule
            Behavior on color { EaseColor {} }
            Behavior on height { Ease { duration: Theme.slow } }
        }
    }

    Repeater {
        model: [0, 6, 12, 18]
        Txt {
            required property int modelData
            x: modelData * chart.pitch
            y: chart.plotHeight + 6
            text: Insights.hourLabel(modelData)
            variant: "caption"
            tone: "tertiary"
        }
    }

    HoverHandler {
        id: hover
        readonly property int index: hovered && point.position.y <= chart.plotHeight
                                     ? Math.max(0, Math.min(23, Math.floor(point.position.x / chart.pitch))) : -1
    }
    PointTip {
        readonly property int count: chart.counts[hover.index] || 0
        readonly property var hour: hover.index >= 0 ? Insights.hourTip(hover.index, count) : null
        shown: hour !== null
        text: hour ? hour.title : ""
        detail: hour ? hour.detail : ""
        pointX: hover.index * chart.pitch + chart.barWidth / 2
        pointY: chart.plotHeight - chart.barHeight(count)
    }
}
