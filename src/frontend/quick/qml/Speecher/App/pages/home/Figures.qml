pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// The period's figures in two columns on a card: what was counted, the
// figure, and a note under it. rows: [{ label, value, note }], filled down
// the first column, then the second. Rows are keyed by label, so a new
// period rolls the figures and fades the notes while the rows hold still.
Item {
    id: grid

    property var rows: []

    readonly property int labelWidth: 92
    readonly property int _perColumn: Math.ceil(rows.length / 2)

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(firstColumn.implicitHeight, secondColumn.implicitHeight)

    Rectangle {
        x: grid.width / 2
        y: Theme.space3
        width: 1
        height: parent.height - 2 * Theme.space3
        color: Theme.rule
    }

    RowList {
        id: firstColumn
        width: grid.width / 2
        first: true
        values: grid.rows.slice(0, grid._perColumn)
        keyOf: r => r.label
        delegate: figure
    }
    RowList {
        id: secondColumn
        x: grid.width / 2
        width: grid.width / 2
        first: true
        values: grid.rows.slice(grid._perColumn)
        keyOf: r => r.label
        delegate: figure
    }

    Component {
        id: figure
        ListRow {
            id: cell
            required property string key
            required property int index
            readonly property var entry: (ListView.view as RowList).entry(key)
            width: ListView.view.width
            first: index === 0
            implicitHeight: Math.max(Theme.rowHeight + Theme.space1, body.implicitHeight + 2 * Theme.space3 + 2)

            Txt {
                id: label
                y: Theme.space3 + 1
                width: grid.labelWidth
                text: cell.entry.label
                tone: "secondary"
            }
            Column {
                id: body
                x: label.width + Theme.space3
                y: Theme.space3 + 1
                width: parent.width - x
                spacing: 2
                MorphText { text: cell.entry.value; variant: "label"; strong: true }
                MorphText { width: parent.width; wrap: true; text: cell.entry.note; variant: "small"; tone: "secondary" }
            }
        }
    }
}
