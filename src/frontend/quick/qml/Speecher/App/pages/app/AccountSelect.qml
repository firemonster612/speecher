import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Picks one of CLI Proxy API's saved accounts of a type (codex or claude)
// for a row; an account that is not signed in says so in the menu, and a
// disabled one shows greyed out, as CLI Proxy API keeps it from use.
Select {
    placeholder: Words.choose
    required property string rowId
    required property string type

    // As wide as a text field, for the email addresses accounts go by.
    implicitWidth: Theme.fieldWidth
    currentId: Settings.values[rowId] || ""
    options: Accounts.cliproxyAccounts.filter(a => a.type === type).map(a => ({
        id: a.id,
        label: a.label,
        help: a.state === "disabled" ? Words.disabledInCliproxy : "",
        badge: a.state === "signedIn" || a.state === "disabled" ? "" : Words.signInStates[a.state],
        enabled: a.state !== "disabled"
    }))
    onChosen: id => Settings.set(rowId, id)
}
