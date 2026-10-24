import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// The floating panel menus open in: it drops under the item that opened it,
// lined up with its right or left edge, and grows from that corner.
T.Popup {
    id: popup

    property string align: "right"   // right or left

    y: parent ? parent.height + 6 : 0
    x: parent && align === "right" ? parent.width - width : 0
    padding: Theme.menuInset
    transformOrigin: align === "right" ? Item.TopRight : Item.TopLeft

    enter: Transition {
        Ease { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        Ease { property: "scale"; from: Theme.motionScale(0.94); to: 1; duration: Theme.slow }
    }
    exit: Transition {
        Ease { property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
        Ease { property: "scale"; to: Theme.motionScale(0.97); duration: Theme.fast; curve: Theme.easeIn }
    }
    background: Surface {}
}
