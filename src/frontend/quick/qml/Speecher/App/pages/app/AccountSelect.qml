import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Picks one of CLI Proxy API's saved accounts of a type (codex or claude)
// for a row; an account that is not signed in says so in the menu. A disabled
// account can't be chosen, so it is listed only while it is the choice.
Select {
    placeholder: Words.choose
    required property string rowId
    required property string type

    // As wide as a text field, for the email addresses accounts go by.
    implicitWidth: Theme.fieldWidth
    currentId: Settings.values[rowId] || ""
    options: Accounts.cliproxyAccounts.filter(a => a.type === type && (a.state !== "disabled" || a.id === currentId)).map(a => ({
        id: a.id,
        label: a.label,
        badge: a.state === "signedIn" ? "" : Words.signInStates[a.state]
    }))
    onChosen: id => Settings.set(rowId, id)
}
