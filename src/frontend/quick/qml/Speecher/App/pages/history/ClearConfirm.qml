import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "HistoryWords.js" as T

// The question before clearing a span of History, with Also delete their
// stats; History's Clear… and General's Clear history both ask it. A clear
// that leaves entries says which audio files kept them and how many went,
// with Try again for the same span, until it all goes or is dismissed.
Column {
    id: root
    property alias ruled: question.ruled
    property alias inset: question.inset
    property string range: ""
    property int count: 0
    // The last clear that left entries: { range, withStats, cleared, left,
    // files }; null for none.
    property var stayed: null

    // Asks about range; false, asking nothing, when it holds nothing.
    function ask(span) {
        stayed = null
        count = History.countIn(span)
        range = count > 0 ? span : ""
        return count > 0
    }
    // Clears span at once, saying what stayed if anything did.
    function clearNow(span, withStats) {
        const result = History.clear(span, withStats)
        stayed = result.left > 0 ? Object.assign({ range: span, withStats: withStats }, result) : null
    }

    width: parent ? parent.width : implicitWidth

    InlineConfirm {
        id: question
        open: root.range !== ""
        text: T.clearQuestion(root.count, root.range)
        detail: T.text.clearDetail
        confirmText: T.text.clearConfirm
        cancelText: Words.cancel
        leading: CheckBox { id: withStats; text: T.text.alsoStats }
        onCancelled: root.range = ""
        onConfirmed: {
            root.clearNow(root.range, withStats.checked)
            root.range = ""
            withStats.checked = false
        }
    }
    Reveal {
        open: root.stayed !== null
        Notice {
            objectName: "clearStayed"
            text: root.stayed ? T.clearStayed(root.stayed) : ""
            tone: "negative"
            inset: question.inset
            ruled: question.ruled
            actionText: T.text.tryAgain
            onAction: root.clearNow(root.stayed.range, root.stayed.withStats)
            Button { text: T.text.dismiss; size: "small"; onClicked: root.stayed = null }
        }
    }
}
