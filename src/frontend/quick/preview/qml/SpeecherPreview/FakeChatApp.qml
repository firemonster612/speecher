import QtQuick
import QtQuick.Effects
import Speecher.Theme
import Speecher.Controls

// A made-up chat app to dictate into, so the popup has something to sit on.
// It stands for another app, so its colours are its own rather than Theme's.
Item {
    id: app
    property string composerText: ""
    property bool selecting: false
    property bool flash: false

    RectangularShadow {
        anchors.fill: frame
        radius: frame.radius
        blur: 40
        offset.y: 18
        color: Qt.rgba(0, 0, 0, 0.45)
    }
    Rectangle {
        id: frame
        anchors.fill: parent
        radius: 14
        color: Theme.dark ? "#18181C" : "#FFFFFF"
        border.width: 1
        border.color: Theme.borderStrong
        clip: true

        // Title bar
        Row {
            x: 16; y: 14; spacing: 7
            Repeater { model: ["#FF5F57", "#FEBC2E", "#28C840"]; Rectangle { required property string modelData; width: 11; height: 11; radius: 6; color: modelData } }
        }
        Txt { anchors.horizontalCenter: parent.horizontalCenter; y: 11; text: "# launch"; variant: "small"; tone: "secondary"; font.weight: Font.DemiBold }

        // Channels
        Rectangle {
            x: 0; y: 38; width: 190; height: parent.height - 38
            color: Theme.dark ? "#141417" : "#F6F5F2"
            Column {
                x: 14; y: 14; spacing: 4
                Txt { text: "Acme"; variant: "heading"; bottomPadding: 10 }
                Repeater {
                    model: ["general", "launch", "design", "random"]
                    Rectangle {
                        required property string modelData
                        width: 162; height: 28; radius: 7
                        color: modelData === "launch" ? Theme.accentSoft : "transparent"
                        Txt { x: 10; anchors.verticalCenter: parent.verticalCenter; text: "#  " + parent.modelData; tone: parent.modelData === "launch" ? "accent" : "secondary"; font.weight: Font.Medium }
                    }
                }
            }
        }

        // Messages
        Column {
            x: 214; y: 58; width: parent.width - 240; spacing: 18
            Repeater {
                model: [
                    { who: "Maya", t: "Onboarding build is up on staging. Would love eyes on it today." },
                    { who: "Jon", t: "Looks great on my end. Two small bugs in the empty states, filing them now." }
                ]
                Row {
                    id: message
                    required property var modelData
                    spacing: 12
                    AppIcon { name: message.modelData.who; size: 34 }
                    Column {
                        spacing: 3
                        Row { spacing: 8; Txt { text: message.modelData.who; strong: true } Txt { text: "10:4" + (message.modelData.who === "Maya" ? "1" : "6"); variant: "small"; tone: "tertiary"; anchors.bottom: parent.bottom } }
                        Txt { text: message.modelData.t; tone: "secondary"; width: 440; wrapMode: Text.WordWrap }
                    }
                }
            }
        }

        // Composer
        Rectangle {
            id: composer
            x: 214; width: parent.width - 238
            anchors.bottom: parent.bottom; anchors.bottomMargin: 20
            height: Math.max(48, typed.implicitHeight + 28)
            radius: 12
            color: app.flash ? Theme.accentSoft : Theme.dark ? "#1F1F24" : "#FAF9F6"
            border.width: 1
            border.color: app.flash ? Theme.accent : Theme.borderStrong
            Behavior on color { EaseColor { duration: Theme.slow } }
            Behavior on border.color { EaseColor { duration: Theme.slow } }
            Behavior on height { Ease {} }
            Rectangle {
                visible: app.selecting
                x: 12; y: 12; width: typed.width; height: typed.height; radius: 3
                color: Theme.soft(Theme.accent, 0.3)
            }
            Txt {
                id: typed
                x: 14; y: 14; width: Math.min(implicitWidth, parent.width - 28)
                text: app.composerText.length ? app.composerText : "Message #launch"
                tone: app.composerText.length ? "primary" : "tertiary"
                wrapMode: Text.WordWrap
            }
            Rectangle {
                width: 2; height: 18; radius: 1; color: Theme.accent
                x: app.composerText.length ? 0 : 13; y: 15
                visible: !app.composerText.length
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    Ease { to: 0; duration: Theme.ms(500); curve: Theme.breathe }
                    Ease { to: 1; duration: Theme.ms(500); curve: Theme.breathe }
                }
            }
        }
    }
}
