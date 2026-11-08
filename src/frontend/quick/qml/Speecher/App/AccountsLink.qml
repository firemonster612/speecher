import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Open Accounts, landing on rowId there, with the way back to where it was
// followed from: the page showing, or the setup assistant when the link is
// in its window, which then opens Main beside it.
TextLink {
    id: link
    required property string rowId
    readonly property bool _inSetup: Window.window?.objectName === "SetupAssistant"

    objectName: "openAccounts"
    text: Words.openAccounts
    onActivated: Navigation.follow("accounts", rowId, _inSetup ? "setup" : "")
}
