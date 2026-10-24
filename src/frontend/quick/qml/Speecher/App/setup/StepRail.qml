pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// The setup steps down the side of the window, one StepNode each. Steps
// behind the current one show a check, or a warning when their check does
// not pass, with what was chosen under them; a pencil line fills down the
// track to the current step.
Item {
    id: rail

    property var steps: []
    property int current: 0
    // The last step that can be picked; the ones after it wait their turn.
    property int limit: steps.length - 1
    signal picked(int index)

    readonly property int _rowHeight: 50
    readonly property int _node: 26

    implicitHeight: steps.length * _rowHeight

    // The track behind the nodes, and its filled part.
    Rectangle {
        x: rail._node / 2 - 1
        y: rail._node / 2
        width: 2
        height: (rail.steps.length - 1) * rail._rowHeight
        radius: 1
        color: Theme.rule
    }
    Rectangle {
        x: rail._node / 2 - 1
        y: rail._node / 2
        width: 2
        height: rail.current * rail._rowHeight
        radius: 1
        color: Theme.accent
        Behavior on height { Ease { duration: Theme.page; curve: Theme.easeInOut } }
    }

    Repeater {
        model: rail.steps
        StepNode {
            required property var modelData
            required property int index
            readonly property bool past: index < rail.current
            // Welcome has nothing to check, so it never warns.
            readonly property bool warn: past && !modelData.done && modelData.id !== "welcome"

            y: index * rail._rowHeight
            width: rail.width
            size: rail._node
            number: index + 1
            title: modelData.title
            detail: past ? modelData.verdict : ""
            stage: warn ? "warning" : past ? "done" : index === rail.current ? "current" : "todo"
            interactive: index <= rail.limit
            onActivated: rail.picked(index)
        }
    }
}
