import QtQuick
import Speecher.Theme

// Wheel and touchpad scrolling for a Flickable, the way the desktop does it.
// Declare it inside the Flickable; it lays itself over the view and takes the
// wheel before the Flickable does, so clicks still pass through.
//
// A touchpad moves the content by exactly the distance the fingers move,
// and when they lift the content coasts on and slows to a stop. A mouse
// wheel moves by the system's lines-per-notch and glides there. Scrolling
// past either end of the view is left to whatever is behind it, so a list
// inside a page hands the page the scroll once it reaches its end.
Item {
    id: scroll

    // The Flickable this was declared in, found once it is complete.
    property Flickable flickable: null

    // How long momentum takes to fade to about a third of its speed, in
    // seconds, and the speed below which it stops, in pixels a second.
    readonly property real friction: 0.45
    readonly property real stopSpeed: 20
    readonly property real maxSpeed: 12000
    // Pixels one line of a mouse wheel moves, and the system's lines per
    // notch (qmllint's type data lacks the style hint, hence the index).
    readonly property real lineHeight: 24
    readonly property int wheelLines: Qt.styleHints["wheelScrollLines"] || 3
    // A touchpad's move. On Linux the angle is the one measure that keeps
    // the desktop's touchpad speed (pixelDelta is rounded to whole pixels
    // and is a sixth or so of what other apps scroll), so it scrolls as
    // the desktop's own Qt apps do: 120 to the lines a wheel notch moves.
    // macOS's pixelDelta is already the system's own scroll.
    readonly property bool _anglesAreTouchpadSpeed: Qt.platform.os === "linux"
    function _touchpadMove(event) {
        if (_anglesAreTouchpadSpeed || event.pixelDelta.y === 0)
            return -event.angleDelta.y / 120 * wheelLines * lineHeight
        return -event.pixelDelta.y
    }

    anchors.fill: parent
    z: 1
    Component.onCompleted: {
        flickable = _owner(parent)
        parent = flickable
    }

    property real _velocity: 0          // px/s, positive scrolls down
    property real _glideTo: NaN         // where a wheel step is heading
    property real _placedY: NaN         // where this last put contentY
    property var _samples: []           // recent touchpad moves: { at, dy }

    function _owner(item) {
        while (item && !(item instanceof Flickable)) item = item.parent
        return item
    }
    function _top() { return flickable.originY }
    function _bottom() { return flickable.originY + Math.max(0, flickable.contentHeight - flickable.height) }
    function _canScroll(dy) {
        return dy < 0 ? flickable.contentY > _top() + 0.5 : flickable.contentY < _bottom() - 0.5
    }
    function _place(y) {
        const clamped = Math.max(_top(), Math.min(_bottom(), y))
        flickable.contentY = clamped
        _placedY = clamped
        return clamped === y
    }
    function _stop() {
        _velocity = 0
        _glideTo = NaN
        _samples = []
    }

    WheelHandler {
        id: wheel
        target: null
        // A WheelHandler takes only mice unless told; a touchpad's scrolls
        // would go to the Flickable's own slow, coastless handling.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        orientation: Qt.Vertical
        acceptedModifiers: Qt.NoModifier
        enabled: scroll.flickable !== null
        onWheel: event => {
            // Only a touchpad reports scroll phases; a wheel, even one that
            // also reports pixels, steps by lines.
            const precise = event.phase !== Qt.NoScrollPhase
            const dy = precise ? scroll._touchpadMove(event)
                               : -event.angleDelta.y / 120 * scroll.wheelLines * scroll.lineHeight

            // Fingers down: stop any coast, and keep the gesture from the
            // Flickable, which would otherwise start its own drag.
            if (event.phase === Qt.ScrollBegin) {
                scroll._stop()
                event.accepted = true
                return
            }
            if (event.phase === Qt.ScrollEnd) {
                scroll._coast()
                event.accepted = true
                return
            }
            if (event.phase === Qt.ScrollMomentum) {
                // The system already coasts (macOS): follow it as it is.
                if (scroll._canScroll(dy)) scroll._place(scroll.flickable.contentY + dy)
                event.accepted = true
                return
            }
            if (dy === 0 || !scroll._canScroll(dy)) {
                scroll._stop()
                event.accepted = false
                return
            }
            event.accepted = true
            if (precise) {
                scroll._velocity = 0
                scroll._glideTo = NaN
                scroll._place(scroll.flickable.contentY + dy)
                const now = Date.now()
                scroll._samples = scroll._samples.filter(s => now - s.at < 100).concat([{ at: now, dy: dy }])
            } else {
                scroll._velocity = 0
                const from = isNaN(scroll._glideTo) ? scroll.flickable.contentY : scroll._glideTo
                scroll._glideTo = Math.max(scroll._top(), Math.min(scroll._bottom(), from + dy))
                if (Theme.movement === 0) {
                    scroll._place(scroll._glideTo)
                    scroll._glideTo = NaN
                }
            }
        }
    }

    // Starts the coast from how fast the fingers were moving as they lifted.
    function _coast() {
        const now = Date.now()
        const recent = _samples.filter(s => now - s.at < 60)
        _samples = []
        if (recent.length < 2) return
        const span = Math.max(16, now - recent[0].at) / 1000
        const moved = recent.reduce((sum, s) => sum + s.dy, 0)
        _velocity = Math.max(-maxSpeed, Math.min(maxSpeed, moved / span))
    }

    FrameAnimation {
        running: scroll._velocity !== 0 || !isNaN(scroll._glideTo)
        onTriggered: {
            const dt = frameTime / Theme.timeScale
            // Something else moved the view (a scroll bar drag, a jump to a
            // search result): let it.
            if (!isNaN(scroll._placedY) && Math.abs(scroll.flickable.contentY - scroll._placedY) > 0.5) {
                scroll._stop()
                return
            }
            if (!isNaN(scroll._glideTo)) {
                const left = scroll._glideTo - scroll.flickable.contentY
                if (Math.abs(left) < 0.5) {
                    scroll._place(scroll._glideTo)
                    scroll._glideTo = NaN
                } else {
                    scroll._place(scroll.flickable.contentY + left * (1 - Math.exp(-dt / 0.06)))
                }
                return
            }
            if (!scroll._place(scroll.flickable.contentY + scroll._velocity * dt)) {
                scroll._velocity = 0
                return
            }
            scroll._velocity *= Math.exp(-dt / scroll.friction)
            if (Math.abs(scroll._velocity) < scroll.stopSpeed) scroll._velocity = 0
        }
    }
}
