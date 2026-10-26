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
    // state, an expired sign-in included, is fixed on Accounts.
    readonly property bool unknown: (Accounts.services.find(s => s.id === providerId) || {}).state === "unknown"
    shown: !!provider.signIn && provider.state !== "ready"
    title: Words.signInTo(provider.label)
    help: unknown ? Words.signInDetail({ state: "unknown" }) : provider.signIn || ""
    Button {
        text: row.unknown ? Words.checkAgain : Words.openAccounts
        size: "small"
        onClicked: row.unknown ? Accounts.recheck(row.providerId) : Navigation.go("accounts")
    }
}
