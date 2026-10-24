pragma Singleton
import QtQuick

// Mock of the settings store: each row's current value by id, starting from
// the schema's defaults. Read values[id] in bindings so they update.
QtObject {
    property var values: ({})

    function set(id, v) {
        const next = Object.assign({}, values)
        next[id] = v
        values = next
    }

    // An action row was pressed; core runs the action the row names.
    signal acted(string id)
    function act(id) { acted(id) }

    // Whether a row's enabledWhen condition holds.
    function rowEnabled(row) {
        if (!row || !row.enabledWhen) return true
        return values[row.enabledWhen.id] === row.enabledWhen.equals
    }

    // The help of the option a choice row holds now, such as what pressing
    // the shortcut does for shortcutBehavior.
    function chosenHelp(id) {
        const o = (Schema.row(id).options || []).find(o => o.id === values[id])
        return o ? o.help || "" : ""
    }

    Component.onCompleted: {
        const start = {}
        for (const r of Schema.rows)
            if (r.value !== undefined) start[r.id] = r.value
        values = start
    }
}
