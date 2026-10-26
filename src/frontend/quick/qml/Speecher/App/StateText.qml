import QtQuick
import Speecher.Controls
import "states.js" as States

// A sign-in or check state from Accounts or System, shown as StatusText:
// stateId picks the tone and the spinner, text words it.
// A passed state is plain text, as everywhere; a dot marks what needs
// attention.
StatusText {
    property string stateId
    indicator: tone === "positive" || tone === "neutral" ? "none" : "dot"

    tone: States.tone(stateId)
    busy: States.busy(stateId)
}
