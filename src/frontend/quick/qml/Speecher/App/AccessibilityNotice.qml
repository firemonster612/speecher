import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The note at the top of a card whose rows need desktop accessibility, with
// the button that turns it on, while it is off and can be turned on. The
// note folds away once accessibility is on. If turning it on fails, the note
// says why instead.
Reveal {
    id: root
    property string text
    readonly property var _check: Health.find(System.checks, "desktopAccessibility")

    open: _check.reason === "off"
    Notice {
        text: Health.failure(root._check) || root.text
        indicator: "icon"
        variant: "small"
        inset: Theme.rowInset
        ruled: false
        Button {
            size: "small"
            text: Words.turnOn
            onClicked: System.fix("desktopAccessibility")
        }
    }
}
