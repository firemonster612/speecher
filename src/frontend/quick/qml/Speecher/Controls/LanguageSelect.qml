import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// The Spoken language's control: a button with the current language's flag
// and name that opens a LanguagePopover to search and choose from, in place
// of a Select. options are the popover's languages. It shows currentId and
// only asks for a change through chosen: the caller sets currentId.
T.AbstractButton {
    id: control

    property var options: []
    property string currentId
    property string placeholder
    property string searchText
    property string clearText
    property string emptyText
    signal chosen(string id)

    readonly property var _current: options.find(o => o.id === currentId) || null

    implicitWidth: Theme.selectWidth
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    Accessible.role: Accessible.ComboBox
    Accessible.name: _current ? _current.label : placeholder
    onClicked: popover.opened ? popover.close() : popover.open()
    Keys.onReturnPressed: popover.open()
    Keys.onEnterPressed: popover.open()

    background: Rectangle {
        id: bg
        radius: Theme.radius
        color: control.pressed ? Theme.surfacePressed : control.hovered || popover.visible ? Theme.surfaceHover : Theme.surface
        border.width: 1
        border.color: control.hovered || popover.visible ? Theme.borderStrong : Theme.border
        Behavior on color { EaseColor {} }
        Behavior on border.color { EaseColor {} }
        FocusRing { target: bg; shown: control.visualFocus }
    }
    contentItem: Item {
        Flag {
            id: flag
            x: 10
            size: 16
            anchors.verticalCenter: parent.verticalCenter
            visible: !!control._current && !control._current.icon
            code: control.currentId
        }
        Icon {
            visible: !!control._current && !!control._current.icon
            anchors.centerIn: flag
            name: control._current && control._current.icon ? control._current.icon : ""
            size: 14
        }
        Txt {
            x: control._current ? flag.x + flag.width + 8 : 11
            width: parent.width - x - 30
            anchors.verticalCenter: parent.verticalCenter
            text: control._current ? control._current.label : control.placeholder
            tone: control._current ? "primary" : "tertiary"
            variant: "label"
            elide: Text.ElideRight
        }
        Icon {
            x: parent.width - width - 10
            anchors.verticalCenter: parent.verticalCenter
            name: "chevrons-up-down"
            size: 14
            color: Theme.textTertiary
        }
    }

    LanguagePopover {
        id: popover
        choosing: true
        languages: control.options
        currentId: control.currentId
        searchText: control.searchText
        clearText: control.clearText
        emptyText: control.emptyText
        onChosen: id => control.chosen(id)
    }
}
