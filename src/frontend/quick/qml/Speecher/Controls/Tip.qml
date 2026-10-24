import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A control's tooltip: after the pointer rests a moment it fades and grows
// in above its parent. detail adds a second, quieter line.
T.ToolTip {
    id: tip

    property bool shown: false
    property string detail

    visible: shown && text.length > 0
    delay: Theme.tipDelay
    y: -implicitHeight - 6
    x: parent ? (parent.width - implicitWidth) / 2 : 0
    padding: 0
    implicitWidth: contentItem.implicitWidth
    implicitHeight: contentItem.implicitHeight
    transformOrigin: Item.Bottom

    contentItem: TipBody { text: tip.text; detail: tip.detail }
    background: null
    enter: Transition {
        Ease { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        Ease { property: "scale"; from: Theme.motionScale(0.94); to: 1 }
    }
    exit: Transition { Ease { property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn } }
}
