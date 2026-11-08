import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "HistoryWords.js" as T

// The question before clearing a span of History, with Also delete their
// stats; History's Clear… and General's Clear history both ask it.
InlineConfirm {
    id: root
    property string range: ""
    property int count: 0

    // Asks about range; false, asking nothing, when it holds nothing.
    function ask(span) {
        count = History.countIn(span)
        range = count > 0 ? span : ""
        return count > 0
    }

    open: range !== ""
    text: T.clearQuestion(count, range)
    detail: T.text.clearDetail
    confirmText: T.text.clearConfirm
    cancelText: Words.cancel
    leading: CheckBox { id: withStats; text: T.text.alsoStats }
    onCancelled: range = ""
    onConfirmed: {
        if (!History.clear(range, withStats.checked))
            Toaster.show(T.text.clearFailed, { icon: "alert-triangle", tone: "negative" })
        range = ""
        withStats.checked = false
    }
}
