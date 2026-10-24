import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// Picks one of CLI Proxy API's saved accounts of a type (codex or claude)
// for a row; expired accounts say so in the menu.
Select {
    placeholder: Words.choose
    required property string rowId
    required property string type

    // As wide as a text field, for the email addresses accounts go by.
    implicitWidth: Theme.fieldWidth
    options: Accounts.cliproxyAccounts.filter(a => a.type === type).map(a => ({
        id: a.id,
        label: a.label,
        help: a.plan,
        badge: a.state === "signedIn" ? "" : Accounts.stateLabels[a.state]
    }))
    currentId: Settings.values[rowId] || ""
    onChosen: id => Settings.set(rowId, id)
}
