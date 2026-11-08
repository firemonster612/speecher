import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The sign-in a refinement provider uses, under the cards that choose it (a
// SignInSourceRow): how to sign in while it isn't ready, or that Accounts
// couldn't read the keychain (a locked one), so the sign-in may well be
// there. Check again asks Accounts, whose last answer (found) stays while it
// checks.
Reveal {
    id: signIn

    property string providerId
    readonly property var provider: ModelText.provider(providerId)
    readonly property bool ready: provider.state === "ready"
    readonly property var _account: Accounts.services.find(s => s.id === providerId) || {}
    readonly property bool unknown: _account.found === "unknown"

    // Not before the first check answers: core says unknown until then, and
    // keeps saying it for an expired sign-in, which Accounts names.
    open: !!provider.signIn && (ready || provider.state !== "unknown" || ["unknown", "expired"].includes(_account.found))
    SignInSourceRow {
        serviceId: signIn.providerId
        ready: signIn.ready
        checking: signIn._account.state === "checking"
        details: signIn.unknown ? signIn._account.detail || "" : ""
        title: signIn.ready ? Words.signIn : signIn.unknown ? Words.signInUnknownFor(signIn.provider.label) : Words.signInTo(signIn.provider.label)
        help: signIn.ready ? "" : signIn.unknown ? Words.keychainUnreadable(signIn._account) : signIn.provider.signIn || ""
        onRecheck: Accounts.recheck(signIn.providerId)
    }
}
