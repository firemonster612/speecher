import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// How to sign in to a refinement provider, under the cards that choose it,
// while its card says it isn't ready. The fix is on Accounts, where API keys
// live too, so the row opens it.
SettingRow {
    id: row
    property string providerId
    readonly property var provider: ModelText.provider(providerId)
    // Accounts couldn't read the keychain (a locked one), so the sign-in
    // may well be there: say so and check again, as Accounts does. Any other
    // state, an expired sign-in included, is fixed on Accounts. found is
    // Accounts' last answer, kept while it checks again.
    readonly property string _found: (Accounts.services.find(s => s.id === providerId) || {}).found || ""
    readonly property bool unknown: _found === "unknown"
    // Not before the first check answers: core says unknown until then, and
    // keeps saying it for an expired sign-in, which Accounts names.
    shown: !!provider.signIn && provider.state !== "ready" && (provider.state !== "unknown" || ["unknown", "expired"].includes(_found))
    title: unknown ? Words.signInUnknownFor(provider.label) : Words.signInTo(provider.label)
    help: unknown ? Words.signInDetail({ state: "unknown" }) : provider.signIn || ""
    Button {
        text: row.unknown ? Words.checkAgain : Words.openAccounts
        size: "small"
        onClicked: row.unknown ? Accounts.recheck(row.providerId) : Navigation.go("accounts")
    }
}
