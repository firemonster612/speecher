pragma Singleton
import QtQuick
import Speecher.Wording

// Which page the window shows, and a row to scroll to and flash once that
// page has loaded (set by the search palette).
QtObject {
    property string current: "home"
    property string pendingRow: ""

    function go(pageId) {
        pendingRow = ""
        current = pageId
    }
    function goToRow(rowId) {
        const row = Schema.row(rowId)
        pendingRow = rowId
        if (current === row.page) pendingRowChanged()
        current = row.page
    }
}
