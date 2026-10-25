import QtQuick
import Speecher.Theme

// The list for rows on a card, and the add, remove and move motion for every
// list: it does not scroll on its own and is as tall as its rows, easing to
// a new height. Give it the data as values, an array; replacing the array
// moves rows instead of rebuilding them, so new rows fade and grow in,
// removed ones fade and shrink out, and the rest glide into their new
// places. keyOf picks an entry's key: its id by default, or the value
// itself for an array of strings. Each delegate gets its key, and entry(key)
// gives its entry back, even while the row is fading out.
//   RowList {
//       id: terms
//       values: Library.terms
//       keyOf: t => t.term
//       delegate: ListRow {
//           required property string key
//           required property int index
//           readonly property var term: terms.entry(key)
//           first: index === 0 && terms.first
//       }
//   }
// first says whether the list starts its card, so its first row leaves out
// the divider as the first row on a card does.
ListView {
    id: list

    property var values: []
    property var keyOf: value => typeof value === "object" ? String(value.id) : String(value)
    property bool first: Positioner.isFirstItem

    function entry(key) { return _entries[key] }
    // Moves a row without touching values, for a drag in progress; keys()
    // reads the new order back.
    function moveRow(from, to) { _rows.move(from, to, 1) }
    function keys() {
        const out = []
        for (let i = 0; i < _rows.count; ++i) out.push(_rows.get(i).key)
        return out
    }

    // The entries by key. A removed key stays until its row has faded out,
    // so the row keeps its content; an entry that comes back replaces its
    // old one.
    property var _entries: ({})
    readonly property ListModel _rows: ListModel {}
    Settle { id: settle }

    function _sync() {
        const next = values.map(keyOf)
        const entries = Object.assign({}, _entries)
        values.forEach((v, i) => entries[next[i]] = v)
        _entries = entries
        for (let i = _rows.count - 1; i >= 0; --i)
            if (next.indexOf(_rows.get(i).key) < 0) { _rows.remove(i); _prune.restart() }
        for (let i = 0; i < next.length; ++i) {
            if (i < _rows.count && _rows.get(i).key === next[i]) continue
            let from = -1
            for (let j = i + 1; j < _rows.count; ++j)
                if (_rows.get(j).key === next[i]) { from = j; break }
            if (from >= 0) _rows.move(from, i, 1)
            else _rows.insert(i, { key: next[i] })
        }
    }
    // Forgets removed keys once their rows are gone. It deletes in place,
    // without a change signal, so a row still on its way out keeps reading
    // its entry.
    readonly property Timer _prune: Timer {
        interval: Theme.slow   // the remove transition runs Theme.base
        onTriggered: {
            const live = list.values.map(list.keyOf)
            for (const key of Object.keys(list._entries))
                if (live.indexOf(key) < 0) delete list._entries[key]
        }
    }
    onValuesChanged: _sync()
    Component.onCompleted: _sync()

    model: _rows
    width: parent ? parent.width : implicitWidth
    implicitHeight: contentHeight
    height: implicitHeight
    interactive: false
    Grow on implicitHeight {}

    // The rows there on load are simply there.
    add: Transition {
        enabled: settle.done
        Ease { property: "opacity"; from: 0; to: 1; duration: Theme.slow }
        Ease { property: "scale"; from: Theme.motionScale(0.96); to: 1; duration: Theme.slow }
    }
    remove: Transition {
        Ease { property: "opacity"; to: 0; duration: Theme.base; curve: Theme.easeIn }
        Ease { property: "scale"; to: Theme.motionScale(0.96); duration: Theme.base; curve: Theme.easeIn }
    }
    // A row displaced while it was still arriving finishes arriving; under
    // reduced motion it takes its new place at once.
    displaced: Transition {
        Ease { property: "y"; duration: Theme.slow * Theme.movement; curve: Theme.springy }
        Ease { property: "opacity"; to: 1; duration: Theme.slow }
        Ease { property: "scale"; to: 1; duration: Theme.slow }
    }
    move: Transition { enabled: Theme.movement > 0; Ease { property: "y"; duration: Theme.slow; curve: Theme.springy } }
    moveDisplaced: Transition { enabled: Theme.movement > 0; Ease { property: "y"; duration: Theme.slow; curve: Theme.springy } }
}
