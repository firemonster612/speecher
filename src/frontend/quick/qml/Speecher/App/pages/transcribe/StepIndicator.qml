pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// Configure, Transcribe, Export: a StepNode for each, spread across the
// page and joined by a ruled line that fills in pencil as the batch moves
// on.
Item {
    id: root

    property var steps: []
    property int current: 0

    implicitHeight: Theme.space6

    Repeater {
        model: root.steps
        Item {
            id: stop
            required property var modelData
            required property int index
            readonly property real slot: root.width / root.steps.length
            x: index * slot
            width: slot
            height: root.height

            StepNode {
                id: node
                anchors.verticalCenter: parent.verticalCenter
                number: stop.index + 1
                title: stop.modelData.label
                stage: stop.index < root.current ? "done" : stop.index === root.current ? "current" : "todo"
            }

            // The line to the next stop.
            Rectangle {
                visible: stop.index < root.steps.length - 1
                x: node.implicitWidth + Theme.space3
                width: stop.slot - x - Theme.space3
                height: 1
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.rule
                Rectangle {
                    // How much of the line is filled; eased on its own so a
                    // change of the page's width does not animate.
                    property real fill: stop.index < root.current ? 1 : 0
                    height: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * fill
                    color: Theme.accent
                    Behavior on fill { Ease { duration: Theme.page; curve: Theme.easeInOut } }
                }
            }
        }
    }
}
