import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The note at the top of a card whose rows need desktop accessibility, with
// the button that turns it on, while it is off and can be turned on. The
// note folds away once accessibility is on.
Reveal {
    id: root
    property alias text: notice.text

    open: System.check("desktopAccessibility").reason === "off"
    Notice {
        id: notice
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
