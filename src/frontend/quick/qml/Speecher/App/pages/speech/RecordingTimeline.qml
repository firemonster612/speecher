pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// A dictation drawn as a strip: the audio kept before you start, your words,
// and the audio kept after you stop. The padding is pencil, since it is what
// the sliders below set, and grows and shrinks with them; with Skip silence
// on, the pause between phrases drops out.
Item {
    id: strip

    property int beforeMs: 0
    property int afterMs: 0
    property int maxMs: 1500
    property bool skipSilence: false
    property string beforeLabel
    property string afterLabel

    implicitHeight: 86

    readonly property real _padMax: width * 0.2
    readonly property real _before: Math.max(6, _padMax * beforeMs / maxMs)
    readonly property real _after: Math.max(6, _padMax * afterMs / maxMs)

    component Pad: Rectangle {
        height: 40
        radius: Theme.radius
        color: Theme.accentSoft
        Behavior on width { Ease { duration: Theme.slow } }
    }
    // A phrase as bars filling its width; seed varies the shape.
    component Phrase: Row {
        id: phrase
        property real seed: 0
        readonly property int count: Math.max(0, Math.floor((width + 3) / 6))
        spacing: 3
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: phrase.count
            Rectangle {
                required property int index
                readonly property real t: index / Math.max(1, phrase.count - 1)
                // Rises and falls like a phrase, with syllables on top.
                readonly property real envelope: Math.sin(Math.PI * t)
                readonly property real v: envelope * (0.45 + 0.55 * Math.abs(Math.sin(index * 1.7 + phrase.seed) * Math.cos(index * 0.43 + phrase.seed)))
                width: 3
                height: 4 + 28 * v
                radius: 1.5
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.textTertiary
            }
        }
    }
    component Marker: Column {
        id: marker
        property string iconName
        spacing: 3
        Rectangle { width: 2; height: 48; radius: 1; color: Theme.textSecondary; anchors.horizontalCenter: parent.horizontalCenter }
        Icon { name: marker.iconName; size: 12; color: Theme.textSecondary; anchors.horizontalCenter: parent.horizontalCenter }
    }

    Rectangle {
        id: track
        y: 22
        width: parent.width
        height: 40
        radius: Theme.radius
        color: Theme.fill

        Pad { id: before; width: strip._before }
        Pad { id: after; width: strip._after; anchors.right: parent.right }

        Item {
            id: words
            anchors.left: before.right
            anchors.right: after.left
            anchors.leftMargin: Theme.space3
            anchors.rightMargin: Theme.space3
            height: parent.height

            Phrase { id: phraseA; width: parent.width * 0.44 }
            Phrase { id: phraseB; width: parent.width * 0.44; seed: 2.1; anchors.right: parent.right }

            // The pause between the phrases: kept as a flat line, or left out.
            Rectangle {
                anchors.left: phraseA.right
                anchors.right: phraseB.left
                anchors.margins: Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                height: strip.skipSilence ? 18 : 2
                radius: height / 2
                color: strip.skipSilence ? Theme.soft(Theme.fillStrong, 0) : Theme.fillStrong
                Behavior on height { Ease {} }
                Behavior on color { EaseColor {} }
                // The outline a left-out pause shows as.
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.borderStrong
                    opacity: strip.skipSilence ? 1 : 0
                    Behavior on opacity { Ease {} }
                }
                Icon {
                    anchors.centerIn: parent
                    name: "scissors"
                    size: 12
                    color: Theme.textTertiary
                    opacity: strip.skipSilence ? 1 : 0
                    Behavior on opacity { Ease {} }
                }
            }
        }
    }

    // Labels over the padding, ticks at the moments you start and stop.
    MorphText {
        x: Math.max(0, before.width / 2 - width / 2)
        text: strip.beforeLabel
        variant: "mono"
        tone: "secondary"
    }
    MorphText {
        x: Math.min(strip.width - width, strip.width - after.width / 2 - width / 2)
        text: strip.afterLabel
        variant: "mono"
        tone: "secondary"
    }
    Marker { x: before.width - width / 2; y: track.y - 4; iconName: "play" }
    Marker { x: strip.width - after.width - width / 2; y: track.y - 4; iconName: "square" }
}
