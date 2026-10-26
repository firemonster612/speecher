pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// The latest words of a dictation, set like captions: each new word rises
// into place, a new line pushes the text up, and older words dim. Words
// never shift sideways, so nothing jumps while you read. maxWords (the
// Live text length setting) decides how many lines show.
// shimmer sweeps blue pencil across the words while they are being refined.
Item {
    id: live
    property string text
    property int maxWords: 12
    readonly property int maxLines: maxWords <= 8 ? 1 : maxWords <= 20 ? 2 : 3
    property bool shimmer: false

    readonly property int lineHeight: 22
    implicitHeight: Math.min(flow.implicitHeight, maxLines * lineHeight)
    clip: true

    ListModel { id: words }
    // The words text held last time, and how many of them have been trimmed
    // off the front of the model.
    property var _all: []
    property int _dropped: 0
    property bool _trimming: false
    // While the width changes (the capsule growing as it morphs), the
    // words wrap onto lines they won't stay on, so nothing is trimmed until
    // it has settled.
    property bool _resizing: false
    onWidthChanged: { _resizing = true; settled.restart() }
    Timer {
        id: settled
        interval: Theme.page
        onTriggered: { live._resizing = false; live._trim() }
    }

    // Keeps the words that are still the same and replaces the rest from the
    // first one that changed, so a word heard in part ("Hel") gives way to
    // the whole of it. A change before the words on show starts over.
    onTextChanged: {
        let all = text.split(/\s+/).filter(w => w.length > 0)
        // Core sends only the last words heard, so the first can drop off the
        // front while the rest carry on; the words before them stay.
        const slid = _slid(all)
        if (slid > 0) all = _all.slice(0, slid).concat(all)
        let same = 0
        while (same < all.length && same < _all.length && all[same] === _all[same]) ++same
        if (same < _dropped) {
            words.clear()
            _dropped = 0
        } else if (same - _dropped < words.count) {
            words.remove(same - _dropped, words.count - (same - _dropped))
        }
        for (let i = _dropped + words.count; i < all.length; ++i) words.append({ word: all[i] })
        _all = all
        Qt.callLater(_trim)
    }

    // How many words off the front of the last text the new one has lost:
    // it starts with the rest of them, the last of which may have changed.
    // 0 when it carries on from the start (its first word can be the same
    // by chance, so that alone says nothing) or is something else.
    function _slid(all) {
        let same = 0
        while (same < all.length && same < _all.length && all[same] === _all[same]) ++same
        if (same >= _all.length - 1) return 0
        for (let k = 1; k < _all.length - 1; ++k) {
            const kept = _all.length - k - 1
            if (kept > all.length) continue
            let match = true
            for (let i = 0; i < kept && match; ++i) match = all[i] === _all[k + i]
            if (match) return k
        }
        return 0
    }

    // Drops whole lines that have scrolled out of view. Removing a full first
    // line leaves every other line wrapped as it was, so only the offset
    // changes, and that change is not animated.
    function _trim() {
        if (_resizing) return
        const hidden = Math.ceil(flow.implicitHeight / lineHeight) - maxLines
        if (hidden < 1 || words.count < 2) return
        let firstLine = 0
        while (firstLine < repeater.count && repeater.itemAt(firstLine).y < lineHeight / 2) ++firstLine
        if (firstLine === 0 || firstLine >= words.count) return
        // Laid out at once, so the offset's change is over before the
        // animation is back on; a later layout would slide the words down.
        _trimming = true
        words.remove(0, firstLine)
        _dropped += firstLine
        flow.forceLayout()
        _trimming = false
    }

    property real _sweep: 0
    Ease on _sweep {
        running: live.shimmer && live.visible
        from: -0.3; to: 1.3
        duration: Theme.ms(1400)
        loops: Animation.Infinite
        curve: Theme.steady
    }

    Flow {
        id: flow
        width: parent.width
        y: live.height - implicitHeight
        spacing: 5
        Behavior on y {
            enabled: !live._trimming && Theme.movement > 0
            Ease {}
        }

        Repeater {
            id: repeater
            model: words
            Txt {
                id: wordItem
                required property string word
                required property int index
                readonly property real age: (words.count - 1 - index) / Math.max(1, live.maxWords)
                readonly property real wave: live.shimmer ? Math.max(0, 1 - Math.abs(live._sweep - index / Math.max(1, words.count)) * 4) : 0
                text: word
                variant: "transcript"
                font.weight: Font.Medium
                lineHeight: live.lineHeight
                lineHeightMode: Text.FixedHeight
                color: wave > 0 ? Qt.tint(Theme.text, Theme.soft(Theme.accent, wave * 0.9)) : Theme.text
                opacity: (1 - Math.min(1, age) * 0.62) * appear
                property real appear: 0
                transform: Translate { id: rise; y: 8 * Theme.movement }
                Component.onCompleted: arrive.start()
                ParallelAnimation {
                    id: arrive
                    Ease { target: wordItem; property: "appear"; to: 1; duration: Theme.slow }
                    Ease { target: rise; property: "y"; to: 0; duration: Theme.slow }
                }
            }
        }
    }
}
