import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// Where a search jump to schema rows lands, for rows a page builds itself.
// Once the page has loaded it opens any Reveal folded around target, scrolls
// target into view and emits landed, so
// the target can flash or open. SchemaRow has one already; give one to a
// custom row, Disclosure, card or section that stands for schema rows:
//   Disclosure { id: more; SearchLanding { rowIds: ["x"]; target: more; onLanded: more.highlight() } }
QtObject {
    id: landing

    required property var rowIds
    required property Item target
    signal landed()

    function _land() {
        // A target in a section folded away, such as the Custom Endpoint's
        // while another service is chosen, opens it first and scrolls once
        // it has grown. Main replaces the page on the next visit, so the
        // section follows its own condition again there.
        let opened = false
        for (let p = target; p; p = p.parent) {
            const reveal = p as Reveal
            if (reveal && !reveal.open) {
                reveal.open = true
                opened = true
            }
        }
        if (opened) {
            _grown.start()
            return
        }
        let p = target.parent
        while (p && !(p as Page)) p = p.parent
        if (p) (p as Page).reveal(target)
        Navigation.pendingRow = ""
        landed()
    }
    // After the page has slid in and laid out.
    readonly property Timer _wait: Timer { interval: Theme.ms(260); onTriggered: landing._land() }
    // After a Reveal it opened has grown, which takes Theme.slow.
    readonly property Timer _grown: Timer { interval: Theme.slow + Theme.fast; onTriggered: landing._land() }
    readonly property Connections _search: Connections {
        target: Navigation
        function onPendingRowChanged() { if (landing.rowIds.includes(Navigation.pendingRow)) landing._wait.restart() }
    }
    Component.onCompleted: if (rowIds.includes(Navigation.pendingRow)) _wait.start()
}
