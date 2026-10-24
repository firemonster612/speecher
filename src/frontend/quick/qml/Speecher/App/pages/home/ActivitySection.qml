pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "words.js" as T

// The year of dictation as a heatmap in the chosen measure, then the hours
// of the day within the chosen period.
Section {
    id: activity

    readonly property var summary: Insights.summary

    title: T.text.activity
    caption: summary ? T.heatmapCaption(summary.activeDaysLastYear) : ""
    actions: SegmentedControl {
        objectName: "heatMeasure"
        options: T.heatMeasures
        currentId: Insights.heatMeasure
        onActivated: id => Insights.heatMeasure = id
    }

    ListRow {
        implicitHeight: year.implicitHeight
        Column {
            id: year
            width: parent.width
            topPadding: Theme.space4
            bottomPadding: Theme.space3
            spacing: Theme.space2
            Heatmap { id: heatmap; width: parent.width }
            Row {
                anchors.right: parent.right
                spacing: Theme.space1
                Txt { text: T.text.less; variant: "caption"; tone: "tertiary"; rightPadding: 2; anchors.verticalCenter: parent.verticalCenter }
                Repeater {
                    model: heatmap.strengths.length
                    Rectangle {
                        required property int index
                        width: 10
                        height: 10
                        radius: Theme.radiusSmall / 2
                        color: heatmap.colorOf(index)
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Txt { text: T.text.more; variant: "caption"; tone: "tertiary"; leftPadding: 2; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }
    ListRow {
        implicitHeight: hours.height + 2 * Theme.space4
        Column {
            id: hourText
            y: Theme.space4
            width: Theme.marginWidth
            spacing: 3
            Txt { text: T.text.timeOfDay; variant: "label" }
            MorphText { width: parent.width; wrap: true; text: activity.summary ? T.hourText(activity.summary) : ""; variant: "small"; tone: "secondary" }
        }
        HourChart {
            id: hours
            x: hourText.width + Theme.space6
            y: Theme.space4
            width: parent.width - x
            counts: activity.summary ? activity.summary.hourCounts : []
            peak: activity.summary ? activity.summary.peakHour : 0
            description: T.hourDescription(activity.summary)
        }
    }
}
