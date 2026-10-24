pragma Singleton
import QtQuick
import Speecher.Theme
import Speecher.Backend

// The colour schemes the Colors setting offers, as { id, label, source }:
// the desktop's own, Theme's presets and the schemes installed on the
// desktop. source is what Theme.colorSource takes; the setting stores id.
QtObject {
    readonly property var system: ({ id: "system", label: "System", source: Appearance.systemColors })
    readonly property var presets: Theme.presets.map(p => ({ id: p.id, label: p.label, source: p }))
    readonly property var installed: Appearance.installedSchemes.map(s => ({ id: "installed:" + s.id, label: s.name, source: s.colors }))

    // The source for a stored id. One no longer installed, or unknown, is
    // Speecher's own first scheme.
    function source(id) {
        const all = [system].concat(presets, installed)
        const found = all.find(o => o.id === id)
        return found ? found.source : Theme.presets[0]
    }
}
