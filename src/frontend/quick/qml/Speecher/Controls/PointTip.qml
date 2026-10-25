import QtQuick
import Speecher.Theme

// A tooltip for a point on a chart: it sits above pointX, pointY (in its
// parent's coordinates), stays inside its parent's width, and glides from
// point to point instead of blinking between them.
TipBody {
    id: tip

    property bool shown: false
    property real pointX
    property real pointY

    z: 10
    x: Math.max(0, Math.min(parent.width - width, pointX - width / 2))
    y: pointY - height - 8
    opacity: shown ? 1 : 0
    scale: shown ? 1 : Theme.motionScale(0.94)
    transformOrigin: Item.Bottom
    enabled: false

    Behavior on opacity { Ease { duration: Theme.fast } }
    Behavior on scale { Ease {} }
    Behavior on x { enabled: tip.opacity > 0.5 && Theme.movement > 0; Ease { duration: Theme.fast } }
    Behavior on y { enabled: tip.opacity > 0.5 && Theme.movement > 0; Ease { duration: Theme.fast } }
}
