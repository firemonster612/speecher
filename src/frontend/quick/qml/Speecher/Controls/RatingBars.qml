pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// Ratings out of ten, one line per measure: its name, ten pips filled in
// color (fractions fill part of a pip), and the score. A measure with no
// value shows unknownText. appearance: compact (thin pips, for cards) or
// wide (rounder, larger pips and "n/10", for a row of its own).
Column {
    id: root

    property var bars: []        // [{ label, value }]; value null when unknown
    property string appearance: "compact"
    property color color: Theme.textSecondary
    property string unknownText: "–"
    property var scoreFormat: v => appearance === "wide" ? v + "/10" : String(v)

    // The ratings as assistive tech says them: "Accuracy 8.5 out of 10,
    // Speed 7 out of 10".
    readonly property string spoken: bars.map(b => b.label + " " + (b.value === null || b.value === undefined
                                                                    ? "unknown" : b.value + " out of 10")).join(", ")

    readonly property bool _wide: appearance === "wide"
    property int pipWidth: _wide ? 14 : 9
    // The measure names' column.
    property int labelWidth: _wide ? 52 : 58

    spacing: _wide ? 8 : 5

    Repeater {
        model: root.bars
        Row {
            id: line
            required property var modelData
            readonly property bool known: modelData.value !== null && modelData.value !== undefined
            readonly property real value: known ? modelData.value : 0

            spacing: root._wide ? 10 : 8
            Txt {
                width: root.labelWidth
                text: line.modelData.label
                variant: root._wide ? "small" : "caption"
                tone: "secondary"
                anchors.verticalCenter: parent.verticalCenter
            }
            Row {
                spacing: root._wide ? 3 : 2
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: 10
                    Rectangle {
                        id: pip
                        required property int index
                        width: root.pipWidth
                        height: root._wide ? 6 : 4
                        radius: root._wide ? 3 : 1
                        color: root._wide ? Theme.fillStrong : Theme.rule
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, line.value - pip.index))
                            height: parent.height
                            radius: parent.radius
                            color: root.color
                            Behavior on width { Ease { duration: Theme.slow } }
                            Behavior on color { EaseColor { duration: Theme.base } }
                        }
                    }
                }
            }
            MorphText {
                width: root._wide ? implicitWidth : 22
                text: line.known ? root.scoreFormat(line.modelData.value) : root.unknownText
                variant: "mono"
                tone: line.known && !root._wide ? "primary" : root._wide ? "secondary" : "tertiary"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
