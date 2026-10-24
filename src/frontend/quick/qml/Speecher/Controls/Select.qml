pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A dropdown. options: [{ id, label, help?, icon?, badge?, enabled? }]. The menu
// grows open from the button's corner and each option can carry a line of
// help; its entries are the same MenuItem a Menu shows, greyed out and not
// chosen when enabled is false. It shows currentId and only asks for a
// change through chosen: the caller sets currentId.
T.ComboBox {
    id: control

    property var options: []
    property string currentId: options.length ? options[0].id : ""
    property string placeholder
    property int menuWidth: Math.max(width, 280)
    signal chosen(string id)

    model: options
    textRole: "label"
    valueRole: "id"
    readonly property int _index: options.findIndex(o => o.id === currentId)
    currentIndex: _index
    // The template moves currentIndex itself, to the pick and to the first
    // entry when the options are replaced; tie it back to currentId, which
    // only the caller changes.
    onActivated: index => {
        if (options[index].enabled !== false) chosen(options[index].id)
        currentIndex = Qt.binding(() => _index)
    }
    onModelChanged: currentIndex = Qt.binding(() => _index)

    implicitHeight: Theme.controlHeight
    implicitWidth: Theme.selectWidth
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    leftPadding: 11
    rightPadding: 30
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }

    readonly property var _current: currentIndex >= 0 ? options[currentIndex] : null

    background: Rectangle {
        id: bg
        radius: Theme.radius
        color: control.pressed ? Theme.surfacePressed : control.hovered || control.popup.visible ? Theme.surfaceHover : Theme.surface
        border.width: 1
        border.color: control.hovered || control.popup.visible ? Theme.borderStrong : Theme.border
        Behavior on color { EaseColor {} }
        Behavior on border.color { EaseColor {} }
        FocusRing { target: bg; shown: control.visualFocus }
    }
    contentItem: Row {
        spacing: 7
        Icon {
            visible: !!(control._current && control._current.icon)
            name: control._current && control._current.icon ? control._current.icon : ""
            size: 14
            anchors.verticalCenter: parent.verticalCenter
        }
        Txt {
            text: control._current ? control._current.label : control.placeholder
            tone: control._current ? "primary" : "tertiary"
            variant: "label"
            elide: Text.ElideRight
            width: Math.min(implicitWidth, control.availableWidth - 22)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    indicator: Icon {
        x: control.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        name: "chevrons-up-down"
        size: 14
        color: Theme.textTertiary
    }

    delegate: MenuItem {
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        text: modelData.label
        iconName: modelData.icon || ""
        help: modelData.help || ""
        badge: modelData.badge || ""
        enabled: modelData.enabled !== false
        current: index === control.currentIndex
        highlighted: control.highlightedIndex === index
    }

    popup: MenuPopup {
        width: control.menuWidth
        implicitHeight: Math.min(list.contentHeight + topPadding + bottomPadding, 360)

        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            spacing: 1
            boundsBehavior: Flickable.StopAtBounds
            // A mouse drags nothing on a desktop; touch still does.
            acceptedButtons: Qt.NoButton
            WheelScroll {}
        }
    }
}
