import QtQuick
import Speecher.Controls
import Speecher.Wording

// How to sign in to a refinement provider, under the cards that choose it,
// while its card says it isn't ready. The fix is on Accounts, where API keys
// live too, so the row opens it.
SettingRow {
    property string providerId
    readonly property var provider: ModelText.provider(providerId)
    shown: !!provider.signIn && provider.state !== "ready"
    title: Words.signInTo(provider.label)
    help: provider.signIn || ""
    Button { text: Words.openAccounts; size: "small"; onClicked: Navigation.go("accounts") }
}
