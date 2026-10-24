pragma Singleton
import QtQuick
import Speecher.Wording

// The preview's settings store: each row's current value by id, starting from
// the schema's values. The app has a C++ Settings with the same members. Read
// values[id] in bindings so they update.
QtObject {
    property var values: ({})

    function set(id, v) {
        const next = Object.assign({}, values)
        next[id] = v
        values = next
    }

    // An action row was pressed. The app runs the action the row names; the
    // preview has nothing to run.
    function act(id) {}

    // Whether a row's enabledWhen condition holds.
    function rowEnabled(row) {
        if (!row || !row.enabledWhen) return true
        return values[row.enabledWhen.id] === row.enabledWhen.equals
    }

    Component.onCompleted: {
        const start = {}
        for (const r of Schema.rows)
            if (r.value !== undefined) start[r.id] = r.value
        values = start
    }
}
