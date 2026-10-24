pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "words.js" as T

// The picture Share copies: the period's words, streak, dictations and audio
// over the year's heatmap, on the page's paper. Home draws it off screen and
// grabs it; it needs Insights to have data.
Rectangle {
    id: card

    readonly property var summary: Insights.summary
    readonly property var figures: [
        { label: "Words", value: Words.num(summary.words) },
        { label: "Streak", value: Words.count(summary.streak.current, "day", "days") },
        { label: "Dictations", value: Words.num(summary.dictations) },
        { label: "Audio", value: T.audioText(summary.audioMs) }
    ]

    width: 760
    height: body.implicitHeight + 2 * Theme.space6
    color: Theme.sheet

    Column {
        id: body
        x: Theme.space6
        y: Theme.space6
        width: card.width - 2 * Theme.space6
        spacing: Theme.space4

        Item {
            width: parent.width
            height: title.height
            Txt { id: title; text: T.share.imageTitle; variant: "heading" }
            Txt {
                anchors.right: parent.right
                anchors.baseline: title.baseline
                text: T.periodName(Insights.period)
                tone: "secondary"
            }
        }
        Row {
            spacing: Theme.space8
            Repeater {
                model: card.figures
                Column {
                    required property var modelData
                    spacing: 2
                    Txt { text: parent.modelData.label; tone: "secondary" }
                    Txt { text: parent.modelData.value; variant: "title" }
                }
            }
        }
        Txt {
            visible: card.summary.wordsPerMinute > 0
            text: card.summary.bookComparison + ", " + Words.num(card.summary.wordsPerMinute) + " words a minute"
            tone: "secondary"
        }
        Heatmap { width: parent.width }
    }
}
