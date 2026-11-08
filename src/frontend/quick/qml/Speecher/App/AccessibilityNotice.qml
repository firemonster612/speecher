import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The note at the top of a card whose rows need desktop accessibility,
// while it is off or this computer has none, with the button that turns it
// on where it can be. Without offersFix the note leaves that button to a
// row on the same page. The note folds away once accessibility is on. If
// turning it on fails, the note says why instead.
Reveal {
    id: root
    property string text
    property bool offersFix: true
    readonly property var _check: Health.find(System.checks, "desktopAccessibility")

    open: _check.reason === "off" || _check.reason === "unsupported"
    Notice {
        text: Health.failure(root._check) || root.text
        indicator: "icon"
        variant: "small"
        inset: Theme.rowInset
        ruled: false
        Button {
            visible: root.offersFix && !!root._check.fix
            size: "small"
            text: Words.turnOn
            onClicked: System.fix("desktopAccessibility")
        }
    }
}
