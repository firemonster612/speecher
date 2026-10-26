pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// The Pasting page's picture: the steps a dictation takes into an app with
// the current settings, ending in the app with the words written in blue
// pencil. When a setting changes the route, the new steps fade in and the
// row recentres, then a dot runs along it once and the words write
// themselves in. A change while that plays lets it carry on; under reduced
// motion the dot stays put and only the words write in.
Plate {
    id: root

    property var steps: []         // [{ icon, label }], tried in order
    property string appName
    property string sample
    property string caption
    // Words under the start, over the first step when there are several,
    // and over each step after it.
    property string startLabel
    property string firstLabel
    property string fallbackLabel

    property real _travel: 1       // 0 to 1 along the route
    property int _typed: sample.length

    height: 156
    clip: true

    readonly property int _pad: Theme.space6
    readonly property real _lineY: 66
    readonly property real _startX: _pad + 20
    readonly property real _endX: appCard.x

    // The route, and the dot that runs along it after a change.
    Rectangle {
        x: root._startX
        y: root._lineY
        width: root._endX - root._startX
        height: 1
        color: Theme.borderStrong
    }
    Rectangle {
        x: root._startX + (root._endX - root._startX) * root._travel - width / 2
        y: root._lineY - height / 2
        width: 8
        height: 8
        radius: 4
        color: Theme.text
        opacity: root._travel > 0 && root._travel < 1 ? 1 : 0
        Behavior on opacity { Ease { duration: Theme.fast } }
    }

    // Start: the dictation.
    Column {
        x: root._startX - width / 2
        y: root._lineY - 20
        spacing: 8
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 40
            height: 40
            radius: 20
            color: Theme.raised
            border.width: 1
            border.color: Theme.borderStrong
            Icon { anchors.centerIn: parent; name: "audio-lines"; size: 18; color: Theme.text }
        }
        Txt { text: root.startLabel; variant: "caption"; tone: "secondary"; anchors.horizontalCenter: parent.horizontalCenter }
    }

    // The steps, centred on the line between the start and the app.
    Row {
        x: root._startX + 20 + (root._endX - root._startX - 20 - width) / 2
        y: root._lineY - 15
        spacing: 18
        Behavior on x { enabled: Theme.movement > 0; Ease { duration: Theme.slow } }
        add: Transition {
            Ease { property: "opacity"; from: 0; to: 1; duration: Theme.slow }
            Ease { property: "scale"; from: Theme.motionScale(0.9); to: 1; duration: Theme.slow; curve: Theme.springy }
        }
        Repeater {
            model: root.steps
            Column {
                id: step
                required property var modelData
                required property int index
                spacing: 8
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: stepRow.implicitWidth + 22
                    height: 30
                    radius: Theme.radius
                    color: Theme.raised
                    border.width: 1
                    border.color: Theme.borderStrong
                    Row {
                        id: stepRow
                        anchors.centerIn: parent
                        spacing: 6
                        Icon { name: step.modelData.icon; size: 14; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                        Txt { text: step.modelData.label; variant: "labelSmall"; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Txt {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: step.index === 0 ? (root.steps.length > 1 ? root.firstLabel : "") : root.fallbackLabel
                    variant: "caption"
                    tone: "secondary"
                }
            }
        }
    }

    // End: the app it lands in.
    Rectangle {
        id: appCard
        x: root.width - width - root._pad
        y: root._lineY - 46
        width: Math.min(260, root.width * 0.34)
        height: 96
        radius: Theme.radiusLarge
        color: Theme.raised
        border.width: 1
        border.color: Theme.borderStrong
        Row {
            x: 12
            y: 10
            spacing: 7
            AppIcon { name: root.appName; size: 18; anchors.verticalCenter: parent.verticalCenter }
            Txt { text: root.appName; variant: "small"; strong: true; anchors.verticalCenter: parent.verticalCenter }
        }
        Rectangle { y: 36; width: parent.width; height: 1; color: Theme.rule }
        // A TextEdit, so the caret can find the end of the text.
        TextEdit {
            id: typed
            x: 12
            y: 46
            width: parent.width - 24
            text: root.sample.slice(0, root._typed)
            readOnly: true
            activeFocusOnPress: false
            color: Theme.accent
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            wrapMode: TextEdit.Wrap
            Caret { edit: typed }
        }
    }

    // Centred under the route it describes, from the first node to the app.
    Txt {
        width: Math.min(implicitWidth, root._endX - root._startX)
        x: (root._startX + root._endX - width) / 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        text: root.caption
        variant: "small"
        tone: "secondary"
    }

    property bool _ready: false
    Component.onCompleted: _ready = true
    onStepsChanged: if (_ready && !replay.running) replay.start()
    SequentialAnimation {
        id: replay
        ScriptAction { script: { root._travel = Theme.movement > 0 ? 0 : 1; root._typed = 0 } }
        Ease { target: root; property: "_travel"; to: 1; duration: Theme.movement > 0 ? Theme.ms(1100) : 0; curve: Theme.easeInOut }
        Ease { target: root; property: "_typed"; from: 0; to: root.sample.length; duration: Theme.ms(16 * root.sample.length); curve: Theme.steady }
    }
}
