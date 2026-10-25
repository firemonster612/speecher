pragma Singleton
import QtQuick
import Speecher.Wording

// Which page the window shows, and a row to scroll to and flash once that
// page has loaded (set by the search palette). requested fires each time a
// page is asked for, the one already showing included, so the sidebar can
// open the group that holds it.
QtObject {
    property string current: "home"
    property string pendingRow: ""
    signal requested(string pageId)

    onCurrentChanged: requested(current)

    function go(pageId) {
        pendingRow = ""
        if (current === pageId) requested(pageId)
        else current = pageId
    }
    function goToRow(rowId) {
        const row = Schema.row(rowId)
        pendingRow = rowId
        if (current === row.page) {
            pendingRowChanged()
            requested(row.page)
        } else {
            current = row.page
        }
    }
}
