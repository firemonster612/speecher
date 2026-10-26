import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The microphone test, leading the microphone page: the live level across
// the stage, and under it what is being heard and the button that runs it.
// The level marks what Skip silence would leave out. With no microphone the
// stage says what to check instead.
Plate {
    id: root

    readonly property var device: Schema.row("audioDevice")
    readonly property var test: Schema.row("microphoneTest")
    readonly property bool running: Audio.testState === "running"
    // Whether the button's test is this page's: setup may hold it too.
    readonly property bool holding: Audio.testHolders.indexOf(root) >= 0
    readonly property int levelHeight: 112

    // The page going away lets go of the microphone.
    Component.onDestruction: Audio.releaseTest(root)

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
                // The Skip silence mark on the level: "Quiet below 2%".
                thresholdText: Schema.row("vadThresholdPercent").label + " "
                    + Schema.valueLabel(Schema.row("vadThresholdPercent"), Settings.values.vadThresholdPercent)
            }

            Item {
                width: parent.width
                height: Math.max(testButton.implicitHeight, status.height)

                StatusText {
                    id: status
                    // Core's reason for a failed test can run long.
                    width: parent.width - testButton.width - Theme.space5
                    wrap: true
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.running || Audio.testError !== "" ? Microphones.testStatus : root.test.help
                    tone: root.running ? "live" : Audio.testError !== "" ? "negative" : "neutral"
                    busy: Audio.testState === "starting"
                    variant: "body"
                }
                Button {
                    id: testButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property bool stops: root.holding && root.running
                    variant: stops ? "secondary" : "primary"
                    iconName: stops ? "square" : "mic"
                    busy: Audio.testState === "starting"
                    enabled: Audio.canToggleTest
                    text: stops ? root.test.stopLabel
                        : Audio.testState === "starting" ? root.test.startingLabel : root.test.startLabel
                    onClicked: stops ? Audio.releaseTest(root) : Audio.holdTest(root)
                }
            }
        }

        // No microphone: what to check, at its own height; the plate eases
        // to the stage's when one appears.
        Item {
            implicitHeight: noMicrophone.implicitHeight
            Column {
                id: noMicrophone
                width: Math.min(parent.width, 420)
                anchors.horizontalCenter: parent.horizontalCenter
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
