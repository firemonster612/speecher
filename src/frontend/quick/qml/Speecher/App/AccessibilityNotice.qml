import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The note at the top of a card whose rows need desktop accessibility,
// while it is off or this computer has none, with the button that turns it
// on where it can be. Every page says the same one sentence of what it
// turns on (Schema.pasting.accessibilityNotice), so one missing permission
// reads as one thing wherever it shows. Without offersFix the note leaves
// that button to a row on the same page; without shown it stays folded
// whatever accessibility does. The note folds away once accessibility is
// on. If turning it on fails, the note says why instead.
Reveal {
    id: root
    property bool offersFix: true
    property bool shown: true
    readonly property var _check: Health.find(System.checks, "desktopAccessibility")

    open: shown && (_check.reason === "off" || _check.reason === "unsupported")
    Notice {
        text: Health.failure(root._check) || Schema.pasting.accessibilityNotice(System.identifiesAppsWithoutAccessibility)
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
