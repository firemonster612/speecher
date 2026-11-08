import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Wording

// What went wrong in the words of the part that failed, behind a Details
// link, so a row says one plain sentence and the specifics stay a click
// away. Folds away while text is empty.
Reveal {
    id: root
    property string text
    property bool expanded: false

    open: text.length > 0
    TextLink {
        text: Words.details
        iconName: root.expanded ? "chevron-down" : "chevron-right"
        onActivated: root.expanded = !root.expanded
    }
    Reveal {
        open: root.expanded
        Txt {
            topPadding: Theme.space1
            width: parent.width
            text: root.text
            variant: "small"
            tone: "secondary"
            wrapMode: Text.Wrap
        }
    }
}
