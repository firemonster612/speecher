pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// Text in columns on a card, one ruled row per entry. columns: [{ width,
// align, tone, variant }]; the column without a width takes what is left.
// values: one array of cell strings per row, keyed by the first cell, so
// new figures roll in place and rows that come or go fold in and out.
RowList {
    id: table

    property var columns: []

    readonly property int _gap: Theme.space4
    readonly property real _rest: width - 2 * Theme.rowInset - columns.reduce((t, c) => t + (c.width || 0), 0) - _gap * (columns.length - 1)
    keyOf: row => row[0]
    delegate: ListRow {
        id: line
        required property string key
        required property int index
        readonly property var cells: table.entry(key)
        width: ListView.view.width
        first: index === 0 && table.first
        implicitHeight: cellRow.implicitHeight + 2 * Theme.space3

        Row {
            id: cellRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: table._gap
            Repeater {
                model: table.columns
                Item {
                    id: column
                    required property var modelData
                    required property int index
                    width: modelData.width || table._rest
                    height: cellText.implicitHeight
                    MorphText {
                        id: cellText
                        anchors.right: column.modelData.align === "right" ? parent.right : undefined
                        text: line.cells[column.index] || ""
                        variant: column.modelData.variant || "body"
                        tone: column.modelData.tone || "primary"
                    }
                }
            }
        }
    }
}
