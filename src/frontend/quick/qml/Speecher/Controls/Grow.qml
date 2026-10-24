import QtQuick
import Speecher.Theme

// A size that eases to its new value, for things that open, close and
// change height: Grow on implicitHeight {}. The first layout snaps instead,
// so a page that has just loaded does not grow its content in. running says
// whether it is easing, for clipping while it does.
Behavior {
    id: grow

    property int duration: Theme.slow
    readonly property bool running: ease.running
    property bool _ready: false

    enabled: _ready
    Component.onCompleted: Qt.callLater(() => _ready = true)
    Ease { id: ease; duration: grow.duration }
}
