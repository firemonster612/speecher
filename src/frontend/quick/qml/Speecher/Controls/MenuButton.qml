import QtQuick
import Speecher.Theme

// A button that drops a menu of actions. items as Menu takes them. confirm
// (text) swaps the caption for a short confirmation with a tick for a
// moment, rolling the changed words in place.
Button {
    id: root

    property string label
    property string leadingIcon
    property var items: []
    property string confirmation
    signal picked(string id)

    function confirm(text) {
        confirmation = text
        settle.restart()
    }

    text: confirmation.length ? confirmation : label
    iconName: confirmation.length ? "check" : leadingIcon
    trailingIcon: confirmation.length ? "" : "chevron-down"
    onClicked: menu.visible ? menu.close() : menu.open()
    Accessible.role: Accessible.ButtonMenu

    Timer { id: settle; interval: Theme.ms(1800); onTriggered: root.confirmation = "" }

    Menu {
        id: menu
        width: 220
        items: root.items
        onPicked: id => root.picked(id)
    }
}
