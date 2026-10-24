import QtQuick
import Speecher.Theme

// Shows one of its children at a time, by currentIndex, and animates the
// change: the old view slides out and fades, the new one slides in from the
// side it sits on, and the height eases to fit. A change in mid-swap carries
// on from where the views are. Pair it with a SegmentedControl for tabs.
// Its implicitWidth is the current view's, easing too, so width:
// implicitWidth sizes it to fit, as for buttons that swap in a row.
Item {
    id: switcher

    property int currentIndex: 0
    default property alias views: holder.data
    // How far views travel sideways while they swap.
    property int travel: 28

    readonly property Item current: holder.children.length > currentIndex ? holder.children[currentIndex] : null
    property Item _previous: null
    property int _previousIndex: 0
    property bool _moving: false

    width: parent ? parent.width : implicitWidth
    implicitWidth: current ? current.implicitWidth : 0
    implicitHeight: current ? current.implicitHeight : 0
    Grow on implicitWidth {}
    height: implicitHeight
    clip: _moving
    Grow on height {}
    property bool _ready: false

    Item {
        id: holder
        width: parent.width
        height: parent.height
    }

    function _place() {
        for (let i = 0; i < holder.children.length; ++i) {
            const view = holder.children[i]
            view.width = Qt.binding(() => holder.width)
            view.visible = i === currentIndex
            view.opacity = 1
            view.x = 0
        }
    }
    Component.onCompleted: { _place(); _previous = current; _previousIndex = currentIndex; Qt.callLater(() => _ready = true) }

    // Reads the new view from children directly: the handler can run before
    // the current binding has caught up.
    onCurrentIndexChanged: {
        const from = _previous
        const to = holder.children.length > currentIndex ? holder.children[currentIndex] : null
        const forward = currentIndex > _previousIndex
        _previousIndex = currentIndex
        if (!_ready || !to || to === from) { _place(); _previous = to; return }
        swap.stop()
        for (const view of holder.children)
            if (view !== from && view !== to) { view.visible = false; view.opacity = 1; view.x = 0 }
        outOpacity.target = from; outX.target = from
        inOpacity.target = to; inX.target = to
        outX.to = (forward ? -travel : travel) * Theme.movement
        // A view still on its way out turns back from where it is.
        inX.from = to.visible ? to.x : (forward ? travel : -travel) * Theme.movement
        if (!to.visible) to.opacity = 0
        to.visible = true
        _moving = true
        swap.start()
        _previous = to
    }

    SequentialAnimation {
        id: swap
        ParallelAnimation {
            Ease { id: outOpacity; property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
            Ease { id: outX; property: "x"; curve: Theme.easeIn }
            SequentialAnimation {
                PauseAnimation { duration: Theme.ms(60) }
                ParallelAnimation {
                    Ease { id: inOpacity; property: "opacity"; to: 1 }
                    Ease { id: inX; property: "x"; to: 0; duration: Theme.slow }
                }
            }
        }
        ScriptAction {
            script: {
                if (outOpacity.target && outOpacity.target !== switcher._previous) outOpacity.target.visible = false
                switcher._moving = false
            }
        }
    }
}
