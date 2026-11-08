import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The delivery steps in the order core tries them, each with what it
// relies on here and its fix: Pasting's Steps section, which its setup
// step shows too. Insert's menu stops keeping accessibility on; Press's
// manages the virtual keyboard once it is set up, with its typing test
// under it.
Section {
    title: Schema.row("insertThroughAccessibility").section
    caption: ["Speecher tries these in order. Turn off any you don't want; one always stays on.", Schema.pasting.noNeeds]
        .filter(s => s).join(" ")
    DeliveryStepRow {
        rowId: "insertThroughAccessibility"
        AccessibilityMenu {}
    }
    DeliveryStepRow {
        rowId: "pressPasteKeys"
        // The virtual keyboard's own menu, once it is set up.
        Reveal {
            sideways: true
            open: System.hasVirtualKeyboard && System.virtualKeyboardActions.length > 0
            anchors.verticalCenter: parent.verticalCenter
            IconButton {
                iconName: "ellipsis"
                size: 28
                tooltip: Words.more
                onClicked: keyboardMenu.open()
                Menu {
                    id: keyboardMenu
                    width: 240
                    items: [
                        { id: "disable", label: "Disable in Speecher", icon: "power-off", help: "Keeps the setup for later." },
                        { id: "remove", label: "Remove setup", icon: "trash-2", help: "Asks for administrator permission." }
                    ].filter(a => System.virtualKeyboardActions.includes(a.id))
                    onPicked: id => id === "remove" ? System.removeVirtualKeyboard() : System.disableVirtualKeyboard()
                }
            }
        }
    }
    VirtualKeyboardTest {}
    DeliveryStepRow { rowId: "keepOnClipboard" }
}
