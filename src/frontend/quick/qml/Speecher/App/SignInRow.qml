import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The sign-in a speech service uses while it isn't ready, under the cards
// that choose it: how to sign in, or why the last check couldn't reach the
// service, and Check again. The chosen card already says its state.
Reveal {
    id: signIn

    property string serviceId
    readonly property var service: ModelText.service(serviceId)
    readonly property bool checking: !!service.signInChecking
    readonly property string failure: service.signInCheckFailure || ""

    // Open once its check says it isn't ready, or couldn't reach it; not
    // while the first check is still out.
    open: !!service.signsIn && service.state !== "ready" && (service.state !== "unknown" || failure !== "")
    SettingRow {
        title: Words.signInTo(signIn.service.label)
        // Why the last check failed wraps under the label, in place of the
        // help, as a refused setting's reason does; else how to sign in.
        help: !signIn.checking && signIn.failure ? signIn.failure : signIn.service.signIn || signIn.service.access
        Button {
            text: Words.checkAgain
            size: "small"
            busy: signIn.checking
            onClicked: Models.checkSignIns()
        }
    }
}
