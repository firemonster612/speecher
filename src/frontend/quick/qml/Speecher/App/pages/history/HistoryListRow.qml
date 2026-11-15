pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "HistoryWords.js" as T

// One of History's results as the page lists it, a row of the page's list:
// the day's heading above the day's first row, then the row on its stretch
// of the day's card. It spans the list, as a ListView places its rows at
// its left edge, and centres its content, contentWidth wide, as the page
// does. Its signals are HistoryRow's, and focused says its row took
// keyboard focus.
Column {
    id: listed

    required property var entry
    required property int index
    property real contentWidth: width
    property alias open: historyRow.open
    readonly property alias row: historyRow
    readonly property bool firstOfDay: ListView.previousSection !== ListView.section
    readonly property bool lastOfDay: ListView.nextSection !== ListView.section
    signal toggled()
    signal readRequested(real atMs)
    signal deleteRequested()
    signal stepFocus(int step)
    signal focused()

    // The list gives its current row focus; the row's keys are on its row.
    onActiveFocusChanged: if (activeFocus) historyRow.rowItem.forceActiveFocus()
    // A Column lays out after the list has placed it, so until then the row
    // counts as a typical one rather than as nothing, which would have the
    // list build every row.
    height: implicitHeight > 0 ? implicitHeight : 80

    // The day's heading, spaced as a Section's.
    Item {
        visible: listed.firstOfDay
        x: (listed.width - width) / 2
        width: listed.contentWidth
        height: Theme.sectionGap + heading.implicitHeight + Theme.space2
        Txt {
            id: heading
            x: 2
            y: parent.height - Theme.space2 - implicitHeight
            width: parent.width - 2
            text: T.dayHeading(listed.entry.day, Insights.today)
            variant: "heading"
            wrapMode: Text.WordWrap
        }
    }
    CardSlice {
        x: (listed.width - width) / 2
        width: listed.contentWidth
        first: listed.firstOfDay
        last: listed.lastOfDay
        HistoryRow {
            id: historyRow
            entry: listed.entry
            first: listed.firstOfDay
            onToggled: listed.toggled()
            onReadRequested: atMs => listed.readRequested(atMs)
            onDeleteRequested: listed.deleteRequested()
            onStepFocus: step => listed.stepFocus(step)
        }
    }
    Connections {
        target: historyRow.rowItem
        function onActiveFocusChanged() { if (historyRow.rowItem.activeFocus) listed.focused() }
    }
}
