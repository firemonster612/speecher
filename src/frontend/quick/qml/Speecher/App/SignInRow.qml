import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The sign-in a speech service uses, under the cards that choose it: where
// it comes from once it works, else how to sign in or why the last check
// couldn't reach the service, with Check again. Either way Accounts, where
// the sign-in is chosen, is a click away. The chosen card already says its
// state.
Reveal {
    id: signIn

    property string serviceId
    readonly property var service: ModelText.service(serviceId)
    readonly property var source: ModelText.signInSource(serviceId)
    readonly property bool ready: service.state === "ready"
    readonly property bool checking: !!service.signInChecking
    readonly property string failure: service.signInCheckFailure || ""

    // Not while the first check is still out.
    open: !!service.signsIn && (ready || service.state !== "unknown" || failure !== "")
    SettingRow {
        title: signIn.ready ? Words.signIn : Words.signInTo(signIn.service.label)
        // Why the last check failed wraps under the label, in place of the
        // help, as a refused setting's reason does; else how to sign in.
        help: signIn.ready ? (signIn.source ? signIn.source.label : "")
              : !signIn.checking && signIn.failure ? signIn.failure : signIn.service.signIn || ""
        Row {
            spacing: Theme.space2
            Button {
                visible: !signIn.ready
                text: Words.checkAgain
                size: "small"
                busy: signIn.checking
                onClicked: Models.checkSignIns()
            }
            Button {
                visible: !!signIn.source
                text: Words.openAccounts
                size: "small"
                onClicked: Navigation.goToRow("accounts", signIn.source.rowId)
            }
        }
    }
}
