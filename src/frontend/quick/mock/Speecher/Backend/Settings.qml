pragma Singleton
import QtQuick
import Speecher.Wording

// The preview's settings store: each row's current value by id, starting from
// the schema's values. The app has a C++ Settings with the same members. Read
// values[id] in bindings so they update.
QtObject {
    property var values: ({})
    // Why a row's last save was refused, by id: the preview refuses nothing.
    readonly property var errors: ({})

    function set(id, v) {
        const next = Object.assign({}, values)
        next[id] = v
        values = next
    }

    // An action row was pressed. The app runs the action the row names; the
    // preview has nothing to run.
    function act(id) {}

    // Whether set saves this row: the preview saves every one.
    function saves(id) { return true }

    // A shortcut row is recording. The app lets go of the bound shortcuts
    // meanwhile; the preview binds none.
    function startRecordingShortcut(id) {}
    function stopRecordingShortcut() {}

    Component.onCompleted: {
        const start = {}
        for (const r of Schema.rows)
            if (r.value !== undefined) start[r.id] = r.value
        values = start
    }
}
