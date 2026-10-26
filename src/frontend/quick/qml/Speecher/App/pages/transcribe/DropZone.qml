import QtQuick
import QtQuick.Shapes
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// Where audio files arrive: a dashed outline that turns pencil while files
// are dragged over it. Large while nothing is listed, a slim strip after;
// the outline eases between the two while their contents cross-fade.
Item {
    id: zone

    property bool compact: false
    readonly property bool over: drop.containsDrag
    signal chooseRequested()

    implicitHeight: layouts.height

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusLarge
        color: zone.over ? Theme.accentSoft : "transparent"
        Behavior on color { EaseColor { duration: Theme.base } }
    }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: zone.over ? Theme.accent : hover.hovered ? Theme.borderStrong : Theme.border
            strokeWidth: 1.5
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            Behavior on strokeColor { EaseColor { duration: Theme.base } }
            PathRectangle { x: 0.75; y: 0.75; width: zone.width - 1.5; height: zone.height - 1.5; radius: Theme.radiusLarge }
        }
    }

    Switcher {
        id: layouts
        travel: 0
        currentIndex: zone.compact ? 1 : 0

        // Large: the icon, the line, the formats and the button.
        Item {
            implicitHeight: 200
            Column {
                anchors.centerIn: parent
                spacing: Theme.space3
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "file-up"
                    size: 26
                    color: zone.over ? Theme.accent : Theme.textTertiary
                }
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.space1
                    Txt { anchors.horizontalCenter: parent.horizontalCenter; text: TranscribeWords.dropToAdd; variant: "heading" }
                    Txt { anchors.horizontalCenter: parent.horizontalCenter; text: TranscribeWords.mediaHint; variant: "small"; tone: "secondary" }
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: TranscribeWords.chooseFiles
                    iconName: "folder-open"
                    onClicked: zone.chooseRequested()
                }
            }
        }

        // Compact: one line and the add button.
        Item {
            implicitHeight: Theme.rowHeight + Theme.space2
            Row {
                x: Theme.space4
                spacing: Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                Icon { name: "file-up"; size: 18; color: zone.over ? Theme.accent : Theme.textTertiary; anchors.verticalCenter: parent.verticalCenter }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Txt { text: TranscribeWords.dropMore; variant: "label" }
                    Txt { text: TranscribeWords.mediaHint; variant: "caption"; tone: "secondary" }
                }
            }
            Button {
                anchors.right: parent.right
                anchors.rightMargin: Theme.space3
                anchors.verticalCenter: parent.verticalCenter
                text: TranscribeWords.addMore
                iconName: "plus"
                size: "small"
                onClicked: zone.chooseRequested()
            }
        }
    }

    HoverHandler { id: hover }
    DropArea {
        id: drop
        anchors.fill: parent
        onDropped: drag => Transcription.addUrls(drag.urls)
    }
}
