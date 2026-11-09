import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One line at the top of a card whose rows need desktop accessibility,
// while it is off or this computer has none: what of the card needs it
// (what, such as "Learning your corrections"), and a link to Home's
// readiness row for it, the one place that says everything it turns on and
// has its fix. Pasting's Insert step shows its state itself, so the line
// stands for that one status everywhere else. It folds away once
// accessibility is on, and without shown whatever accessibility does.
Reveal {
    id: root
    property string what
    property bool shown: true
    // The link's words; Home, which holds the row, says it is above.
    property string linkText: Schema.pasting.accessibilityLink
    readonly property var _check: Health.find(System.checks, "desktopAccessibility")

    open: shown && (_check.reason === "off" || _check.reason === "unsupported")
    LinkedText {
        objectName: "accessibilityLink"
        x: Theme.rowInset
        width: parent.width - Theme.rowInset * 2
        topPadding: Theme.space3
        bottomPadding: Theme.space1
        plainText: Schema.pasting.accessibilityLine(root.what, root._check.reason, root.linkText)
        link: root._check.fix ? root.linkText : ""
        variant: "small"
        tone: "secondary"
        wrapMode: Text.WordWrap
        onActivated: Navigation.follow("home", "desktopAccessibility")
    }
}
