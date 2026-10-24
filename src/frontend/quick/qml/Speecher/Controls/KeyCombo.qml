pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// A shortcut as a row of key caps, such as ["Ctrl", "Space"]. size is
// KeyCap's. pressed sinks every cap and lit inks them, as when the shortcut
// is being held. Keys that join the combination pop in; the rest slide to
// make room.
Row {
    id: combo

    property var keys: []
    property string size: "medium"
    property bool pressed: false
    property bool lit: false

    spacing: size === "large" ? 10 : 4
    add: Transition {
        Ease { property: "opacity"; from: 0; to: 1 }
        Ease { property: "scale"; from: Theme.motionScale(0.6); to: 1; duration: Theme.slow; curve: Theme.springy }
    }
    move: Transition { enabled: Theme.movement > 0; Ease { property: "x"; duration: Theme.slow } }

    Repeater {
        model: combo.keys
        KeyCap {
            required property string modelData
            text: modelData
            size: combo.size
            pressed: combo.pressed
            lit: combo.lit
        }
    }
}
