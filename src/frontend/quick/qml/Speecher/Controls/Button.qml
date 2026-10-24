import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A push button. variant: primary, secondary, ghost, danger. size: small,
// medium, large. iconName goes before the text, trailingIcon after it, and
// trailing takes an item to end the button with, such as the KeyCombo of its
// key; busy swaps the icon for a spinner. When the text changes, only the
// changed words roll, and the button eases to its new width.
T.Button {
    id: control

    property string variant: "secondary"
    property string size: "medium"
    property string iconName
    property string trailingIcon
    property bool busy: false
    property string tooltip
    property alias trailing: trailingSlot.data

    readonly property int _h: size === "small" ? 26 : size === "large" ? 38 : Theme.controlHeight
    readonly property bool _primary: variant === "primary"
    readonly property color _ink: _primary ? Theme.sheet : variant === "danger" ? Theme.negative : Theme.text

    implicitHeight: _h
    implicitWidth: Math.max(_h, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: text.length ? (size === "small" ? 10 : 14) : 0
    rightPadding: leftPadding
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    Accessible.description: tooltip

    scale: down ? Theme.motionScale(0.965) : 1
    Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }
    Behavior on opacity { Ease {} }

    background: Rectangle {
        id: bg
        radius: Theme.radius
        color: {
            switch (control.variant) {
            case "primary": return control.down ? Theme.textPressed : control.hovered ? Theme.textHover : Theme.text
            case "danger": return control.down ? Theme.soft(Theme.negative, 0.22) : control.hovered ? Theme.soft(Theme.negative, 0.14) : "transparent"
            case "ghost": return control.down ? Theme.fillStrong : control.hovered ? Theme.fill : "transparent"
            default: return control.down ? Theme.surfacePressed : control.hovered ? Theme.surfaceHover : Theme.raised
            }
        }
        border.width: control.variant === "secondary" || control.variant === "danger" ? 1 : 0
        border.color: control.variant === "danger" ? Theme.soft(Theme.negative, 0.45) : control.hovered ? Theme.borderStrong : Theme.border
        Behavior on color { EaseColor {} }
        Behavior on border.color { EaseColor {} }
        FocusRing { target: bg; shown: control.visualFocus }
    }

    contentItem: Row {
        spacing: 6
        // The icon and the spinner share one place and cross-fade; it opens
        // and closes when only one of them is there.
        Item {
            id: mark
            readonly property int size: control.size === "small" ? 14 : 16
            width: control.busy || control.iconName.length > 0 ? size : 0
            height: size
            anchors.verticalCenter: parent.verticalCenter
            Behavior on width { Ease {} }
            Spinner {
                anchors.centerIn: parent
                size: 14
                color: control._primary ? Theme.sheet : Theme.textSecondary
                opacity: control.busy ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease {} }
            }
            Icon {
                anchors.centerIn: parent
                name: control.iconName
                size: mark.size
                color: control._ink
                opacity: control.busy ? 0 : 1
                Behavior on opacity { Ease {} }
            }
        }
        MorphText {
            visible: control.text.length > 0
            text: control.text
            variant: control.size === "small" ? "labelSmall" : "label"
            color: control._ink
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: control.trailingIcon.length > 0
            name: control.trailingIcon
            size: 14
            color: control._primary ? Theme.sheet : Theme.textSecondary
            anchors.verticalCenter: parent.verticalCenter
        }
        Row { id: trailingSlot; anchors.verticalCenter: parent.verticalCenter }
    }

    Tip { text: control.tooltip; shown: control.hovered }
}
