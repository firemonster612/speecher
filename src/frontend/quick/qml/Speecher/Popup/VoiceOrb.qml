pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// The popup's status mark. Listening, it is a red record dot with a ring
// that swells with the voice; then a spinner, a blue pencil, a tick or a
// warning as the dictation moves on. Besides the popup's phases, phase takes
// the receipts that are not a plain delivery: copied, fallback, cancelled.
Item {
    id: mark
    property string phase: "listening"
    property real level: 0
    property int size: 30

    implicitWidth: size
    implicitHeight: size

    // Record dot and its level ring.
    Rectangle {
        anchors.centerIn: parent
        width: mark.size * 0.72
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1.5
        border.color: Theme.negative
        opacity: mark.phase === "listening" ? 0.25 + mark.level * 0.55 : 0
        scale: Theme.motionScale(mark.phase === "listening" ? 0.75 + mark.level * 0.55 : 0.6)
        Behavior on scale { Ease { duration: Theme.ms(90) } }
        Behavior on opacity { Ease { duration: Theme.ms(90) } }
    }
    Rectangle {
        anchors.centerIn: parent
        width: mark.size * 0.34
        height: width
        radius: width / 2
        color: Theme.negative
        opacity: mark.phase === "listening" ? 1 : 0
        scale: mark.phase === "listening" ? 1 : Theme.motionScale(0.3)
        Behavior on opacity { Ease {} }
        Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
    }

    Spinner {
        anchors.centerIn: parent
        size: mark.size * 0.62
        lineWidth: 2
        color: Theme.text
        opacity: mark.phase === "starting" || mark.phase === "transcribing" ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { Ease {} }
    }

    // One icon per later phase, popping in as the phase changes.
    Repeater {
        model: [
            { phase: "paused", icon: "pause", tint: "secondary" },
            { phase: "refining", icon: "pencil-line", tint: "accent" },
            { phase: "review", icon: "pencil-line", tint: "accent" },
            { phase: "delivered", icon: "check", tint: "positive" },
            { phase: "copied", icon: "clipboard-check", tint: "positive" },
            { phase: "fallback", icon: "info", tint: "secondary" },
            { phase: "cancelled", icon: "x", tint: "secondary" },
            { phase: "error", icon: "triangle-alert", tint: "negative" }
        ]
        Icon {
            id: phaseIcon
            required property var modelData
            readonly property bool on: mark.phase === phaseIcon.modelData.phase
            anchors.centerIn: parent
            name: phaseIcon.modelData.icon
            size: Math.round(mark.size * 0.6)
            color: Theme.tone(phaseIcon.modelData.tint)
            opacity: on ? 1 : 0
            scale: on ? 1 : Theme.motionScale(0.4)
            rotation: on ? 0 : -30 * Theme.movement
            Behavior on opacity { Ease {} }
            Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
            Behavior on rotation { Ease { duration: Theme.slow; curve: Theme.springy } }
        }
    }
}
