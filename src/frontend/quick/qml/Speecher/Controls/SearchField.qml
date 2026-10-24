import QtQuick
import Speecher.Theme

// A filter field: a search icon, and a clear button once there is text.
TextField {
    id: field

    property string clearText

    iconName: "search"

    trailing: IconButton {
        iconName: "x"
        size: 22
        tooltip: field.clearText
        enabled: field.length > 0
        opacity: enabled ? 1 : 0
        scale: enabled ? 1 : Theme.motionScale(0.6)
        Behavior on opacity { Ease { duration: Theme.fast } }
        Behavior on scale { Ease { duration: Theme.fast } }
        onClicked: { field.clear(); field.forceActiveFocus() }
    }
}
