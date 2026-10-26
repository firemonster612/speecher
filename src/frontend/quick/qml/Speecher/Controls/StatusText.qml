import QtQuick
import Speecher.Theme

// A state in plain words, such as "Signed in" or "Checking…". A state that
// needs attention (warning, negative) takes its colour and a dot; one in
// progress (busy) gets a spinner. live is sound being heard: a pulsing dot
// in the live-sound red beside text that stays secondary. tertiary is for
// states that are only waiting their turn. indicator: auto (as described),
// dot (a dot for every settled state, for lists read at a glance) or none.
// Changes of wording roll in place and the indicator fades between kinds.
// wrap sets the text to the width left beside the indicator, over several
// lines; maximumLineCount and elide bound it.
Row {
    id: root

    property string text
    property string tone: "neutral"     // neutral, tertiary, positive, warning, negative, accent, live
    property bool busy: false
    property string indicator: "auto"
    property string variant: "small"
    property bool wrap: false
    property alias maximumLineCount: words.maximumLineCount
    property alias elide: words.elide

    readonly property bool _attention: tone === "warning" || tone === "negative"
    readonly property bool _live: tone === "live"
    readonly property bool _dot: !busy && indicator !== "none" && (_attention || _live || indicator === "dot")

    spacing: mark.width > 0 ? 7 : 0

    Item {
        id: mark
        width: root.busy || root._dot ? 12 : 0
        height: 12
        // Beside the first line when the text wraps, else the middle.
        anchors.verticalCenter: root.wrap ? undefined : parent.verticalCenter
        y: root.wrap ? Math.max(0, (words.implicitHeight / Math.max(1, words.lineCount) - height) / 2) : 0
        Behavior on width { Ease {} }
        Spinner {
            anchors.centerIn: parent
            size: 12
            opacity: root.busy ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { Ease {} }
        }
        StatusDot {
            anchors.centerIn: parent
            size: 7
            color: root._live ? Theme.negative : Theme.tone(root.tone)
            pulse: root._live
            opacity: root._dot ? 1 : 0
            scale: root._dot ? 1 : Theme.motionScale(0.4)
            Behavior on opacity { Ease {} }
            Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
        }
    }
    MorphText {
        id: words
        width: root.wrap ? root.width - mark.width - root.spacing : implicitWidth
        wrap: root.wrap
        text: root.text
        variant: root.variant
        color: root._attention || root.tone === "accent" || root.tone === "tertiary" ? Theme.tone(root.tone) : Theme.textSecondary
        anchors.verticalCenter: root.wrap ? undefined : parent.verticalCenter
    }
}
