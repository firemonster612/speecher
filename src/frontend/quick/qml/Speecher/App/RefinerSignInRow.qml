import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The sign-in a refinement provider uses, under the cards that choose it:
// where it comes from once it works, else how to sign in. The fix is on
// Accounts, where API keys live too, so the row opens its Sign-in row.
SettingRow {
    id: row
    property string providerId
    readonly property var provider: ModelText.provider(providerId)
    readonly property var source: ModelText.signInSource(providerId)
    readonly property bool ready: provider.state === "ready"
    // Accounts couldn't read the keychain (a locked one), so the sign-in
    // may well be there: say so and check again, as Accounts does. Any other
    // state, an expired sign-in included, is fixed on Accounts. found is
    // Accounts' last answer, kept while it checks again.
    readonly property var _account: Accounts.services.find(s => s.id === providerId) || {}
    readonly property string _found: _account.found || ""
    readonly property bool unknown: _found === "unknown"
    // Not before the first check answers: core says unknown until then, and
    // keeps saying it for an expired sign-in, which Accounts names.
    shown: !!provider.signIn && (ready || provider.state !== "unknown" || ["unknown", "expired"].includes(_found))
    title: ready ? Words.signIn : unknown ? Words.signInUnknownFor(provider.label) : Words.signInTo(provider.label)
    help: ready ? (source ? source.label : "") : unknown ? Words.keychainUnreadable(_account) : provider.signIn || ""
    Button {
        text: row.unknown ? Words.checkAgain : Words.openAccounts
        size: "small"
        onClicked: row.unknown ? Accounts.recheck(row.providerId) : Navigation.goToRow("accounts", row.source.rowId)
    }
}
