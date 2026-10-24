import QtQuick
import Speecher.Theme

// Text that animates only what changes: the characters both texts share at
// the start and end hold still, and the part between them rolls out as the
// new part rolls in, while the line eases to its new width. Numbers roll up
// as they grow and down as they shrink. Use it for counts, statuses, titles
// and notes that change in place; at rest it is a plain Txt.
// wrap sets it to the width over several lines. Rolling works on one line,
// so a change where either text needs more than one fades out and back in
// instead, and the height eases to the new wording's. maximumLineCount and
// elide limit it as they do a Txt.
Item {
    id: root

    property string text
    property string variant: "body"
    property string tone: "primary"
    property bool strong: false
    property color color: Theme.tone(tone)
    property bool wrap: false
    property alias maximumLineCount: whole.maximumLineCount
    property alias elide: whole.elide
    readonly property alias truncated: whole.truncated

    // While a change rolls, the line is split into a still head and tail
    // and the changing middle.
    property bool _rolling: false
    property string _head
    property string _from
    property string _to
    property string _tail
    property real _travel: 0
    // Text set while the item is still loading is shown as it is.
    property bool _ready: false
    Component.onCompleted: { whole.text = text; Qt.callLater(() => _ready = true) }

    implicitWidth: _rolling ? head.implicitWidth + middle.width + tail.implicitWidth : whole.implicitWidth
    implicitHeight: whole.implicitHeight
    height: implicitHeight
    clip: grow.running
    Grow on height { id: grow }

    onTextChanged: _change()
    function _fits(s) { measure.text = s; return measure.advanceWidth <= width + 0.5 }
    function _change() {
        if (!_ready) { whole.text = text; return }
        const before = _rolling ? _head + _to + _tail : whole.text
        roll.stop()
        fade.stop()
        const after = text
        const bounded = wrap || elide !== Text.ElideNone
        if (bounded && !(_fits(before) && _fits(after))) {
            _rolling = false
            fade.start()
            return
        }
        let start = 0
        while (start < before.length && start < after.length && before[start] === after[start]) ++start
        let end = 0
        while (end < before.length - start && end < after.length - start
               && before[before.length - 1 - end] === after[after.length - 1 - end]) ++end
        const a = Number(before.replace(/[^0-9.\-]/g, "")), b = Number(after.replace(/[^0-9.\-]/g, ""))
        _travel = (before.length && after.length && !isNaN(a) && !isNaN(b) && b < a ? -1 : 1) * Math.round(whole.implicitHeight * 0.6) * Theme.movement
        _head = after.slice(0, start)
        _from = before.slice(start, before.length - end)
        _to = after.slice(start, after.length - end)
        _tail = after.slice(after.length - end)
        whole.text = after
        whole.opacity = 1
        widthAnim.from = outgoing.implicitWidth
        widthAnim.to = incoming.implicitWidth
        _rolling = true
        roll.start()
    }

    Txt {
        id: whole
        width: root.wrap || root.elide !== Text.ElideNone ? root.width : implicitWidth
        variant: root.variant
        strong: root.strong
        color: root.color
        wrapMode: root.wrap ? Text.WordWrap : Text.NoWrap
        visible: !root._rolling
    }
    TextMetrics { id: measure; font: whole.font }

    Row {
        visible: root._rolling
        Txt { id: head; text: root._head; variant: root.variant; strong: root.strong; color: root.color }
        Item {
            id: middle
            width: 0
            height: head.height
            clip: true
            Txt { id: outgoing; text: root._from; variant: root.variant; strong: root.strong; color: root.color }
            Txt { id: incoming; text: root._to; variant: root.variant; strong: root.strong; color: root.color; opacity: 0 }
        }
        Txt { id: tail; text: root._tail; variant: root.variant; strong: root.strong; color: root.color }
    }

    SequentialAnimation {
        id: roll
        ParallelAnimation {
            Ease { id: widthAnim; target: middle; property: "width"; duration: Theme.slow }
            Ease { target: outgoing; property: "y"; from: 0; to: -root._travel; duration: Theme.base; curve: Theme.easeIn }
            Ease { target: outgoing; property: "opacity"; from: 1; to: 0; duration: Theme.fast; curve: Theme.easeIn }
            Ease { target: incoming; property: "y"; from: root._travel; to: 0; duration: Theme.slow }
            Ease { target: incoming; property: "opacity"; from: 0; to: 1; duration: Theme.slow }
        }
        // Back to one plain line.
        ScriptAction { script: { root._rolling = false; middle.width = 0 } }
    }
    SequentialAnimation {
        id: fade
        Ease { target: whole; property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
        ScriptAction { script: whole.text = root.text }
        Ease { target: whole; property: "opacity"; to: 1 }
    }
}
