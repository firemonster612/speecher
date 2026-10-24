import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A single-line text field. iconName adds a leading icon; secret hides the
// text behind an eye toggle; trailing takes small buttons at the right end,
// such as clear or a suggestions chevron. variant sets the text in one of
// Txt's styles, such as title for a name edited where a title would be.
T.TextField {
    id: control

    property string iconName
    property string variant: "body"
    property bool secret: false
    property string showText
    property string hideText
    property alias trailing: trailingSlot.data
    property bool _revealed: false

    implicitWidth: Theme.fieldWidth
    implicitHeight: Theme.controlHeight
    leftPadding: iconName.length ? 32 : 11
    rightPadding: trailingRow.width > 0 ? trailingRow.width + 9 : 11
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accentRing
    selectedTextColor: Theme.text
    font: style.font
    echoMode: secret && !_revealed ? TextInput.Password : TextInput.Normal
    passwordCharacter: "•"
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }

    Txt { id: style; visible: false; variant: control.variant }
    // Templates draws no placeholder of its own.
    Txt {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        text: control.placeholderText
        variant: control.variant
        tone: "tertiary"
        elide: Text.ElideRight
        visible: !control.length && !control.preeditText
    }

    background: InputFrame {
        focused: control.activeFocus
        hovered: control.hovered
        Icon {
            visible: control.iconName.length > 0
            name: control.iconName
            size: 15
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            color: control.activeFocus ? Theme.text : Theme.textTertiary
        }
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.rightMargin: 5
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Row { id: trailingSlot; spacing: 2 }
        IconButton {
            visible: control.secret
            iconName: control._revealed ? "eye-off" : "eye"
            size: 24
            tooltip: control._revealed ? control.hideText : control.showText
            onClicked: control._revealed = !control._revealed
        }
    }
}
