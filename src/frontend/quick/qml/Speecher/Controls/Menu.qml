pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T

// A menu that drops open under the item it is declared in. items:
// [{ id, label, icon?, help?, badge?, enabled? }]; the one whose id is
// currentId gets a check. Arrow keys move, Enter picks, Escape closes.
MenuPopup {
    id: menu

    property var items: []
    property string currentId
    property int maxHeight: 340
    signal picked(string id)

    function _pick(index) {
        const entry = items[index]
        if (!entry || entry.enabled === false) return
        close()
        picked(entry.id)
    }

    width: 260
    implicitHeight: Math.min(list.contentHeight + topPadding + bottomPadding, maxHeight)
    focus: true
    // A press on what opened the menu is left to its toggle; closing on it
    // too would open the menu again when the press outlasts the close.
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
    onOpened: { list.currentIndex = -1; list.forceActiveFocus() }

    contentItem: ListView {
        id: list
        clip: true
        implicitHeight: contentHeight
        model: menu.items
        spacing: 1
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drags nothing on a desktop; touch still does.
        acceptedButtons: Qt.NoButton
        WheelScroll {}
        keyNavigationEnabled: true
        Keys.onReturnPressed: menu._pick(currentIndex)
        Keys.onEnterPressed: menu._pick(currentIndex)
        delegate: MenuItem {
            required property var modelData
            required property int index
            width: ListView.view.width
            text: modelData.label
            iconName: modelData.icon || ""
            help: modelData.help || ""
            badge: modelData.badge || ""
            current: modelData.id === menu.currentId
            enabled: modelData.enabled !== false
            highlighted: ListView.isCurrentItem
            onHoveredChanged: if (hovered) list.currentIndex = index
            onClicked: menu._pick(index)
        }
    }
}
