import QtQuick
import Speecher.Theme

// An edit marked up like a proof, as refinement makes it: added words in
// blue pencil on a faint wash, removed ones struck through in the margin's
// red. segments: [{ kind: same|added|removed, text }], whose spacing and
// line breaks are kept. mode: marked (the
// marks), spoken (what was said: removed words kept, added ones left out) or
// result (what is pasted). Changing mode or segments cross-fades the text and
// eases the height to fit.
Item {
    id: root

    property var segments: []
    property string mode: "marked"
    property string variant: "body"

    // One formatter for every proof mark in the app. The text keeps its own
    // spacing; only where a mode leaves a word out does the gap close up.
    function html(segments, mode) {
        const esc = RichText.escape
        // The segments the mode shows. A word left out leaves the spaces
        // either side of it: keep one, and none before punctuation or at the
        // start.
        const shown = []
        let dropped = false
        for (const s of segments || []) {
            if ((s.kind === "added" && mode === "spoken") || (s.kind === "removed" && mode === "result")) {
                dropped = true
                continue
            }
            let text = s.text
            const before = shown.length ? shown[shown.length - 1] : null
            if (dropped && (!before || before.text.endsWith(" "))) {
                text = text.replace(/^ +/, "")
                if (text === "") continue
                if (before && /^[.,;:!?]/.test(text)) before.text = before.text.slice(0, -1)
            }
            dropped = false
            shown.push({ kind: s.kind, text: text })
        }
        // Rich text would fold runs of spaces into one.
        return "<span style='white-space:pre-wrap;'>" + shown.map(s => {
            if (mode === "marked" && s.kind === "added")
                return "<span style='background-color:" + Theme.css(Theme.accent, 0.12) + "; color:" + Theme.css(Theme.accent) + ";'>" + esc(s.text) + "</span>"
            if (mode === "marked" && s.kind === "removed")
                return "<s style='color:" + Theme.css(Theme.negative, 0.75) + ";'>" + esc(s.text) + "</s>"
            return esc(s.text)
        }).join("").replace(/\n/g, "<br>") + "</span>"
    }

    readonly property string _html: html(segments, mode)
    // Two layers take turns: the new text fades in on one as the old fades
    // out on the other.
    property bool _onA: true

    width: parent ? parent.width : implicitWidth
    implicitHeight: _onA ? a.implicitHeight : b.implicitHeight
    Grow on implicitHeight {}

    on_HtmlChanged: {
        (_onA ? b : a).text = _html
        _onA = !_onA
    }
    Component.onCompleted: a.text = _html

    Txt {
        id: a
        width: parent.width
        variant: root.variant
        textFormat: Text.RichText
        wrapMode: Text.WordWrap
        opacity: root._onA ? 1 : 0
        Behavior on opacity { Ease { duration: Theme.slow; curve: Theme.easeInOut } }
    }
    Txt {
        id: b
        width: parent.width
        variant: root.variant
        textFormat: Text.RichText
        wrapMode: Text.WordWrap
        opacity: root._onA ? 0 : 1
        Behavior on opacity { Ease { duration: Theme.slow; curve: Theme.easeInOut } }
    }
}
