pragma Singleton
import QtQuick
import Speecher.Theme
import Speecher.Backend

// The colour schemes the Colors setting offers, as { id, label, source }:
// the desktop's own (which the page labels), Theme's presets and the schemes
// installed on the desktop. source is what Theme.colorSource takes; the
// setting stores id.
QtObject {
    // The desktop's colours, with the platform palette's accent where the
    // desktop names none of its own.
    readonly property var system: ({ id: "system", source: Object.assign({ accent: _palette.accent }, Appearance.systemColors) })
    readonly property var presets: Theme.presets.map(p => ({ id: p.id, label: p.label, source: p }))
    readonly property var installed: Appearance.installedSchemes.map(s => ({ id: "installed:" + s.id, label: s.name, source: s.colors }))

    // The source for a stored id. One no longer installed, or unknown, is
    // Speecher's own first scheme.
    function source(id) {
        const all = [system].concat(presets, installed)
        const found = all.find(o => o.id === id)
        return found ? found.source : Theme.presets[0]
    }

    readonly property SystemPalette _palette: SystemPalette {}
}
