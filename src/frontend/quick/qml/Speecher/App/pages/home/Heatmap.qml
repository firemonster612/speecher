pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The year of activity: one cell per day, Monday at the top, in pencil as
// strong as the chosen measure. Cells change colour in place when the
// measure changes; a ring and a tip follow the pointer from day to day.
Item {
    id: map

    property string measure: "dictations"
    readonly property var levels: Insights.heatLevels(measure)
    // The pencil strength of each level, from no dictation to the busiest
    // quarter; the legend under the map shows the same steps.
    readonly property var strengths: [0, 0.24, 0.45, 0.7, 1]

    readonly property int labelWidth: 30
    readonly property int headHeight: 20
    readonly property real gap: 3
    readonly property int weeks: Insights.weeks
    readonly property real cell: Math.max(6, (width - labelWidth - gap * (weeks - 1)) / weeks)
    readonly property real pitch: cell + gap

    function colorOf(level) { return level === 0 ? Theme.fill : Theme.soft(Theme.accent, strengths[level]) }

    implicitHeight: headHeight + 7 * cell + 6 * gap
    Accessible.role: Accessible.Chart
    Accessible.name: Insights.heatmapCaption

    Repeater {
        model: Insights.monthLabels
        Txt {
            required property var modelData
            x: map.labelWidth + modelData.column * map.pitch
            text: modelData.label
            variant: "caption"
            tone: "tertiary"
        }
    }
    Repeater {
        model: Insights.heatRowLabels
        Txt {
            required property string modelData
            required property int index
            y: map.headHeight + index * map.pitch
            height: map.cell
            verticalAlignment: Text.AlignVCenter
            text: modelData
            variant: "caption"
            tone: "tertiary"
        }
    }

    Repeater {
        model: Insights.heatmap.length
        Rectangle {
            required property int index
            x: map.labelWidth + Math.floor(index / 7) * map.pitch
            y: map.headHeight + (index % 7) * map.pitch
            width: map.cell
            height: map.cell
            radius: Math.min(Theme.radiusSmall - 1, map.cell / 4)
            color: map.colorOf(map.levels[index] || 0)
            Behavior on color { EaseColor { duration: Theme.slow } }
        }
    }

    HoverHandler {
        id: hover
        readonly property int column: Math.floor((point.position.x - map.labelWidth) / map.pitch)
        readonly property int row: Math.floor((point.position.y - map.headHeight) / map.pitch)
        readonly property int index: hovered && column >= 0 && row >= 0 && row < 7 && column * 7 + row < Insights.heatmap.length
                                     ? column * 7 + row : -1
    }

    // The ring around the hovered day.
    Rectangle {
        id: ring
        property int day: 0
        x: map.labelWidth + Math.floor(day / 7) * map.pitch - 2
        y: map.headHeight + (day % 7) * map.pitch - 2
        width: map.cell + 4
        height: map.cell + 4
        radius: Theme.radiusSmall
        color: "transparent"
        border.width: 1.5
        border.color: Theme.text
        opacity: hover.index >= 0 ? 1 : 0
        Behavior on opacity { Ease { duration: Theme.fast } }
        // Holds the last day while it fades out.
        Binding on day { when: hover.index >= 0; value: hover.index; restoreMode: Binding.RestoreNone }
    }

    PointTip {
        readonly property var day: hover.index >= 0 ? Insights.dayTip(hover.index, map.measure) : null
        shown: day !== null
        text: day ? day.title : ""
        detail: day ? day.detail : ""
        pointX: ring.x + ring.width / 2
        pointY: ring.y
    }
}
