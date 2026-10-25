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
// X11) to inputRegion whenever it changes, frame by frame as the capsule
// morphs, so clicks on the transparent room around them reach the app
// underneath. Once the popup is empty, faded out, the host hides the window.
Window {
    id: window

    readonly property bool empty: popup.empty
    readonly property int shadowRoom: 32
    // What takes the pointer, in the window's coordinates.
    readonly property rect inputRegion: Qt.rect(popup.touchable.x + shadowRoom, popup.touchable.y + shadowRoom,
                                               popup.touchable.width, popup.touchable.height)

    width: Math.ceil(popup.largest.width) + shadowRoom * 2
    height: Math.ceil(popup.largest.height) + shadowRoom * 2
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.Tool

    DictationPopup { id: popup; anchors.fill: parent; anchors.margins: window.shadowRoom }
}
