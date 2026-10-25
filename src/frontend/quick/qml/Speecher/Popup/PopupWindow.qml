import QtQuick
import QtQuick.Window

// The window that carries the popup in the app: frameless, transparent and
// never focused. Core places it (a layer-shell surface on Wayland, a panel
// above other windows elsewhere) near the bottom of the screen. It is as big
// as the capsule ever grows, with room around it for the shadow, and the
// capsule morphs inside it from the bottom centre: resizing the window as
// the capsule animates would redo the surface every frame, and hosts that
// keep the top-left corner fixed would shift the capsule.
//
// Only the capsule, and the offers over it, take the pointer. The host sets
// the window's input region (the surface's on Wayland, the input shape on
// X11, the pointer's pass-through on macOS) to inputRegion whenever it
// changes, frame by frame as the capsule morphs, so clicks on the
// transparent room around them reach the app underneath. On Windows, where
// the mask also clips drawing, it follows shadowRegion instead. Once the
// popup is empty, faded out, the host hides the window.
Window {
    id: window

    readonly property bool empty: popup.empty
    readonly property int shadowRoom: 32
    // What takes the pointer, in the window's coordinates.
    readonly property rect inputRegion: Qt.rect(popup.touchable.x + shadowRoom, popup.touchable.y + shadowRoom,
                                               popup.touchable.width, popup.touchable.height)
    // The input region and as far as the capsule's shadow reaches, which is
    // shifted down.
    readonly property rect shadowRegion: {
        const reach = popup.shadow.blur + popup.shadow.spread
        const left = Math.min(inputRegion.x, popup.capsule.x + shadowRoom - reach)
        const top = Math.min(inputRegion.y, popup.capsule.y + shadowRoom - reach + popup.shadow.offset.y)
        const right = Math.max(inputRegion.x + inputRegion.width, popup.capsule.x + shadowRoom + popup.capsule.width + reach)
        const bottom = Math.max(inputRegion.y + inputRegion.height,
                                popup.capsule.y + shadowRoom + popup.capsule.height + reach + popup.shadow.offset.y)
        return Qt.rect(left, top, right - left, bottom - top)
    }

    width: Math.ceil(popup.largest.width) + shadowRoom * 2
    height: Math.ceil(popup.largest.height) + shadowRoom * 2
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.Tool

    DictationPopup { id: popup; anchors.fill: parent; anchors.margins: window.shadowRoom }
}
