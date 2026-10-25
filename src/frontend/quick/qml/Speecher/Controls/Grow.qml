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
    property Settle _settle: Settle {}

    // The engine can let go of the Settle as it fires, by which time the
    // first layout is behind it.
    enabled: !_settle || _settle.done
    Ease { id: ease; duration: grow.duration }
}
