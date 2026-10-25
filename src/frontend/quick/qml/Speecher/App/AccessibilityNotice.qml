import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// The note at the top of a card whose rows need desktop accessibility, with
// the button that turns it on. The button spins and can't be pressed again
// while the fix runs, and the note folds away once accessibility is on.
Reveal {
    id: root
    property alias text: notice.text
    readonly property bool _working: System.check("desktopAccessibility").state === "working"

    open: !System.accessibility
    Notice {
        id: notice
        indicator: "icon"
        variant: "small"
        inset: Theme.rowInset
        ruled: false
        Button {
            size: "small"
            text: Words.turnOn
            busy: root._working
            enabled: !root._working
            onClicked: System.fix("desktopAccessibility")
        }
    }
}
