import QtQuick
import Speecher.Controls
import "states.js" as States

// A sign-in or check state from Accounts or System, shown as StatusText:
// stateId picks the tone and the spinner, text words it.
StatusText {
    property string stateId

    tone: States.tone(stateId)
    busy: States.busy(stateId)
}
