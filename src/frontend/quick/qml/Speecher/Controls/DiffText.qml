import QtQuick
import Speecher.Theme

// An edit marked up like a proof, as refinement makes it: added words in
// blue pencil on a faint wash, removed ones struck through in the margin's
// red. segments: [{ kind: same|added|removed, text }], whose line breaks
// are kept. mode: marked (the
// marks), spoken (what was said: removed words kept, added ones left out) or
// result (what is pasted). Changing mode or segments cross-fades the text and
// eases the height to fit.
Item {
    id: root

    property var segments: []
    property string mode: "marked"
    property string variant: "body"

    // One formatter for every proof mark in the app.
    function html(segments, mode) {
        const esc = RichText.escape
        return (segments || []).map(s => {
            if (s.kind === "added")
                return mode === "marked" ? "<span style='background-color:" + Theme.css(Theme.accent, 0.12) + "; color:" + Theme.css(Theme.accent) + ";'>" + esc(s.text) + "</span>"
                     : mode === "result" ? esc(s.text) : ""
            if (s.kind === "removed")
                return mode === "marked" ? "<s style='color:" + Theme.css(Theme.negative, 0.75) + ";'>" + esc(s.text) + "</s>"
                     : mode === "spoken" ? esc(s.text) : ""
            return esc(s.text)
        }).join("").replace(/ {2,}/g, " ").replace(/ ([.,;:!?])/g, "$1").replace(/\n/g, "<br>")
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
