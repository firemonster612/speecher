import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The microphone test, leading the microphone page: the live level across
// the stage, and under it what is being heard and the button that runs it.
// The level marks what Skip silence would leave out. With no microphone the
// stage says what to check instead.
Plate {
    id: root

    readonly property var device: Schema.row("audioDevice")
    readonly property var test: Schema.row("microphoneTest")
    readonly property bool running: Audio.testState === "running"
    readonly property int levelHeight: 112

    implicitHeight: views.height + Theme.space6 * 2

    Switcher {
        id: views
        x: Theme.space6
        y: Theme.space6
        width: parent.width - Theme.space6 * 2
        currentIndex: Audio.hasMicrophones ? 0 : 1

        Column {
            spacing: Theme.space5

            Waveform {
                width: parent.width
                height: root.levelHeight
                bars: 72
                gap: 3
                interval: Theme.ms(45)
                fitWidth: true
                compress: true
                fadeOlder: true
                active: root.running
                level: Audio.level
                threshold: Settings.values.vadEnabled ? (Settings.values.vadThresholdPercent || 0) / 100 : -1
                thresholdText: Audio.thresholdText
            }

            Item {
                width: parent.width
                height: testButton.implicitHeight

                StatusText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.running ? Audio.testStatus : root.test.help
                    tone: root.running ? "live" : "neutral"
                    busy: Audio.testState === "starting"
                    variant: "body"
                }
                Button {
                    id: testButton
                    anchors.right: parent.right
                    variant: root.running ? "secondary" : "primary"
                    iconName: root.running ? "square" : "mic"
                    busy: Audio.testState === "starting"
                    text: Audio.testCaption
                    onClicked: Audio.toggleTest()
                }
            }
        }

        // No microphone: what to check, as tall as the stage it stands in for.
        Item {
            implicitHeight: root.levelHeight + Theme.space5 + Theme.controlHeight
            Column {
                width: Math.min(parent.width, 420)
                anchors.centerIn: parent
                spacing: Theme.space3
                Txt {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.device.disabledHelp
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.device.disabledActionLabel
                    iconName: "refresh-cw"
                    size: "small"
                    onClicked: Audio.refreshDevices()
                }
            }
        }
    }
}
