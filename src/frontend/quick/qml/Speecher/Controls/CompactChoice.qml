pragma ComponentBehavior: Bound
import QtQuick

// One choice from a list, in the control its length calls for: segments for
// two to four short options, a Select for more, so a list that grows past
// four (custom cleanup levels) turns into a menu everywhere at once. stretch
// spreads the segments over the width it is given.
Loader {
    id: root

    property var options: []
    property string currentId
    property string placeholder
    property bool stretch: false
    signal chosen(string id)

    sourceComponent: options.length <= 4 ? segments : menu

    Component {
        id: segments
        SegmentedControl { stretch: root.stretch; options: root.options; currentId: root.currentId; onActivated: id => root.chosen(id) }
    }
    Component {
        id: menu
        Select { placeholder: root.placeholder; options: root.options; currentId: root.currentId; onChosen: id => root.chosen(id) }
    }
}
