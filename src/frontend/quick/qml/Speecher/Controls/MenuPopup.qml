import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// The floating panel menus open in: it drops under the item that opened it,
// lined up with its right or left edge, and grows from that corner, kept
// inside the window.
T.Popup {
    id: popup

    property string align: "right"   // right or left

    // As wide as the widest of entries ([{ label, icon?, help? }]) shows its
    // label whole with its icon (or, with flags, every entry's flag) and
    // check, as MenuItem lays them out, and the scroll bar's gutter: at
    // least atLeast, 280 where an entry has help to wrap, and at most
    // Theme.menuMaxWidth, past which a label elides. The labels are measured
    // ahead, and once laid out the entries say how wide they really are
    // (measure), which a platform's text can make wider.
    function fitWidth(entries, atLeast, flags) {
        const label = entries.reduce((w, e) => Math.max(w, labelFont.advanceWidth(e.label || "") + (e.icon || flags ? 24 : 0)), 0)
        const item = Math.max(10 + Math.ceil(label) + 24 + 10, measured)
        const least = Math.max(atLeast, entries.some(e => e.help) ? 280 : 0)
        return Math.max(least, Math.min(Theme.menuMaxWidth, item + 10 + leftPadding + rightPadding))
    }
    // The widest entry as laid out; an entry's delegate reports its
    // implicitWidth here. Reset it when the entries change.
    property real measured: 0
    function measure(entryWidth) { if (entryWidth > measured) measured = entryWidth }
    FontMetrics {
        id: labelFont
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        font.weight: Font.Medium
    }

    y: parent ? parent.height + 6 : 0
    x: parent && align === "right" ? parent.width - width : 0
    padding: Theme.menuInset
    margins: Theme.space2
    transformOrigin: align === "right" ? Item.TopRight : Item.TopLeft

    enter: Transition {
        Ease { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        Ease { property: "scale"; from: Theme.motionScale(0.94); to: 1; duration: Theme.slow }
    }
    exit: Transition {
        Ease { property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
        Ease { property: "scale"; to: Theme.motionScale(0.97); duration: Theme.fast; curve: Theme.easeIn }
    }
    background: Surface {}
}
