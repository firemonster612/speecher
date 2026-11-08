import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// A chosen service's status, under the cards that choose it (setup and
// Transcribe files): while it can't run, what stands in its way and how to
// sign in where that is the fix, with Fix, which opens its Services card in
// a sheet over the page, and Check again for a sign-in. Folded away while
// it is ready, and for a service with nothing to fix on Services.
Reveal {
    id: status

    // "speech" or "refinement".
    required property string role
    property string serviceId
    readonly property var service: ModelText.serviceOf(role, serviceId)
    // ChatGPT's or Claude's refinement sign-in as Accounts last found it,
    // which knows an expired one core still calls unknown.
    readonly property var account: role === "refinement" ? Accounts.services.find(s => s.id === serviceId) || {} : {}

    objectName: "serviceStatus"
    // Not before the first check answers: a sign-in nobody has seen is
    // unknown, not missing.
    open: serviceId !== "" && serviceId !== "none" && service.state !== "ready" && ModelText.cardOf(serviceId) !== ""
          && (ModelText.fixable(service) || ["missing", "expired", "unknown"].includes(account.found))

    SettingRow {
        title: ModelText.status(status.service)
        help: status.service.signsIn || status.service.signIn ? status.service.signIn || "" : ""
        Row {
            spacing: Theme.space3
            Button {
                visible: !!status.service.signsIn || !!status.service.signIn
                text: Words.checkAgain
                size: "small"
                busy: ModelText.checkingSignIn(status.role, status.service)
                onClicked: ModelText.checkSignIn(status.role, status.serviceId)
            }
            Button {
                objectName: "fixService"
                text: Words.fix
                variant: "primary"
                size: "small"
                onClicked: sheet.openFor(status.role, status.serviceId)
            }
        }
    }
    FixSheet { id: sheet }
}
