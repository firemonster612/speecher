import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// Where a search jump to schema rows lands, for rows a page builds itself.
// Once the page has loaded it scrolls target into view and emits landed, so
// the target can flash or open. SchemaRow has one already; give one to a
// custom row, Disclosure, card or section that stands for schema rows:
//   Disclosure { id: more; SearchLanding { rowIds: ["x"]; target: more; onLanded: more.highlight() } }
QtObject {
    id: landing

    required property var rowIds
    required property Item target
    signal landed()

    function _land() {
        let p = target.parent
        while (p && !(p as Page)) p = p.parent
        if (p) (p as Page).reveal(target)
        Navigation.pendingRow = ""
        landed()
    }
    // After the page has slid in and laid out. QML empties a landing it has
    // begun destroying while its Timer can still fire; such a landing has no
    // _land, so it does nothing.
    readonly property Timer _wait: Timer { interval: Theme.ms(260); onTriggered: if (landing._land) landing._land() }
    readonly property Connections _search: Connections {
        target: Navigation
        function onPendingRowChanged() { if (landing.rowIds.includes(Navigation.pendingRow)) landing._wait.restart() }
    }
    Component.onCompleted: if (rowIds.includes(Navigation.pendingRow)) _wait.start()
}
