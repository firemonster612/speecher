import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The typing test that turns the virtual keyboard on, under its health row.
// Run test empties and focuses the field, Speecher types into it, and the
// keyboard turns on once the words arrive there; nothing turns it on before.
Reveal {
    id: root
    readonly property string _test: System.virtualKeyboardTest
    readonly property bool _missed: _test === "missed"

    open: _test !== "off"
    Notice {
        indicator: "icon"
        variant: "small"
        tone: root._missed ? "negative" : "info"
        inset: Theme.rowInset
        text: root._missed ? "The words didn't reach the field. Leave it focused and try again."
                           : "Speecher types a few words into this field to check it can type for you."
        TextField {
            id: field
            objectName: "virtualKeyboardTestField"
            width: 150
            anchors.verticalCenter: parent.verticalCenter
            onTextChanged: System.virtualKeyboardTestReceived(text)
        }
        Button {
            text: "Run test"
            size: "small"
            busy: root._test === "typing"
            enabled: !busy
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                field.text = ""
                field.forceActiveFocus()
                System.runVirtualKeyboardTest()
            }
        }
        Button {
            text: Words.cancel
            size: "small"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: System.cancelVirtualKeyboardTest()
        }
    }
}
