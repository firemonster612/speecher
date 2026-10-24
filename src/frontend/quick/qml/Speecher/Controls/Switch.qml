import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// An on/off switch. A tap or Space flips it, and the knob can be dragged:
// let go past halfway and it flips (the template's own drag). The knob is
// drawn between two edges that ease on their own: pressing stretches it
// toward the side it will go and holds it there, and on a flip the leading
// edge goes first while the trailing one follows with a little overshoot, so
// the knob stretches, travels and settles. Under reduced motion it neither
// stretches nor travels, and the colours still fade.
T.Switch {
    id: control

    implicitWidth: 36
    implicitHeight: 22
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }

    indicator: Rectangle {
        id: track
        // position follows a drag; at rest it is checked's 0 or 1, so the
        // colours show where the knob will land before it is let go.
        readonly property bool on: control.position > 0.5
        width: control.width
        height: control.height
        radius: height / 2
        readonly property color shade: on ? (control.hovered ? Theme.accentHover : Theme.accent)
                                          : (control.hovered ? Theme.fillStrong : Theme.fill)
        // Pressing tints the track toward fillStrong, whichever way it is set.
        color: control.pressed ? Qt.tint(shade, Theme.fillStrong) : shade
        border.width: on ? 0 : 1
        border.color: Theme.border
        Behavior on color { EaseColor {} }

        Rectangle {
            id: knob
            readonly property int inset: 3
            readonly property real size: track.height - inset * 2
            readonly property real held: size + 5 * Theme.movement
            readonly property real start: inset
            readonly property real end: track.width - inset
            // Carried by a drag: held away from where it rests. Not
            // "between the ends", since a drag past an end clamps position
            // to that end and the knob must stay there until let go.
            readonly property bool dragging: control.pressed && control.position !== (control.checked ? 1 : 0)
            readonly property real centre: Math.max(start + held / 2, Math.min(end - held / 2, control.position * track.width))
            readonly property real reach: control.pressed ? held : size
            property real leftEdge: dragging ? centre - held / 2 : control.checked ? end - reach : start
            property real rightEdge: dragging ? centre + held / 2 : control.checked ? end : start + reach
            // A drag keeps the knob under the pointer, so it lags less.
            readonly property int lead: dragging ? Theme.ms(60) : Theme.ms(170)
            readonly property int trail: dragging ? Theme.ms(110) : Theme.ms(320)

            x: leftEdge
            y: inset
            width: Math.max(size * 0.8, rightEdge - leftEdge)
            height: size
            radius: height / 2
            color: track.on ? Theme.sheet : Theme.raised
            Behavior on color { EaseColor { duration: Theme.base } }
            // The edge moving toward where the knob goes leads; the other
            // trails and overshoots a little as it lands. Under reduced
            // motion both move at once.
            Behavior on leftEdge {
                id: leftMove
                enabled: Theme.movement > 0
                Ease {
                    readonly property bool leads: leftMove.targetValue < knob.leftEdge
                    duration: leads ? knob.lead : knob.trail
                    curve: leads ? Theme.easeOut : Theme.springy
                }
            }
            Behavior on rightEdge {
                id: rightMove
                enabled: Theme.movement > 0
                Ease {
                    readonly property bool leads: rightMove.targetValue > knob.rightEdge
                    duration: leads ? knob.lead : knob.trail
                    curve: leads ? Theme.easeOut : Theme.springy
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                z: -1
                radius: height / 2
                color: "transparent"
                border.color: Theme.knobEdge
                border.width: 1
                visible: !track.on
            }
        }
        FocusRing { target: track; shown: control.visualFocus }
    }
    contentItem: Item {}
}
