import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The sign-in a speech service uses, under the cards that choose it (a
// SignInSourceRow): how to sign in while it isn't ready, also while the
// first check is still out, or that the last check couldn't reach the
// service.
Reveal {
    id: signIn

    property string serviceId
    readonly property var service: ModelText.service(serviceId)
    readonly property bool ready: service.state === "ready"
    readonly property bool checking: !!service.signInChecking
    readonly property string failure: checking ? "" : service.signInCheckFailure || ""

    open: !!service.signsIn
    SignInSourceRow {
        serviceId: signIn.serviceId
        ready: signIn.ready
        checking: signIn.checking
        details: signIn.failure
        title: signIn.ready ? Words.signIn : Words.signInTo(signIn.service.label)
        help: signIn.ready ? "" : signIn.failure ? Words.signInUnreachable(signIn.service.label) : signIn.service.signIn || ""
        onRecheck: Models.checkSignIns()
    }
}
