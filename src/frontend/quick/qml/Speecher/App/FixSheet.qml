pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "pages/services"

// The Services card that fixes one service, over the page that found it
// can't run, so the person never leaves that page: ChatGPT's or Claude's
// sign-in, This computer's part for the job, or the job's server. Open
// Services (or Open This computer) leads to the whole page.
// openFor(role, serviceId) opens it.
T.Popup {
    id: sheet

    property string role
    property string serviceId
    readonly property string card: ModelText.cardOf(serviceId)

    function openFor(role, serviceId) {
        sheet.role = role
        sheet.serviceId = serviceId
        open()
    }
    // From setup it opens Main beside the assistant, leading back to it.
    function openPage() {
        const fromSetup = sheet.contentItem.Window.window?.objectName === "SetupAssistant"
        sheet.close()
        Navigation.follow(ModelText.cardPage(sheet.serviceId), "", fromSetup ? "setup" : "")
    }

    objectName: "fixSheet"
    parent: T.Overlay.overlay
    modal: true
    focus: true
    width: Math.min(parent ? parent.width - Theme.space6 * 2 : 640, 640)
    // Templates have no size of their own.
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(Theme.space6, (parent.height - height) / 3) : 0
    padding: Theme.space6
    transformOrigin: Item.Center

    enter: Transition {
        Ease { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
        Ease { property: "scale"; from: Theme.motionScale(0.96); to: 1; duration: Theme.slow }
    }
    exit: Transition {
        Ease { property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
        Ease { property: "scale"; to: Theme.motionScale(0.98); duration: Theme.fast; curve: Theme.easeIn }
    }
    // The sheet is a small page: page paper, so the card on it reads as one.
    background: Surface { color: Theme.sheet; radius: Theme.radiusPanel }
    T.Overlay.modal: Rectangle {
        color: Theme.scrim
        Behavior on opacity { Ease { duration: Theme.fast } }
    }

    contentItem: Column {
        spacing: Theme.space4
        Item {
            width: parent.width
            height: Math.max(heading.implicitHeight, close.height)
            Txt {
                id: heading
                text: ModelText.serviceOf(sheet.role, sheet.serviceId).label
                variant: "title"
                anchors.verticalCenter: parent.verticalCenter
            }
            IconButton {
                id: close
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "x"
                tooltip: Words.shell.close
                onClicked: sheet.close()
            }
        }
        Loader {
            id: card
            width: parent.width
            active: sheet.opened || sheet.visible
            sourceComponent: sheet.card === "chatgpt" || sheet.card === "claude" ? signIn
                           : sheet.card === "computer" ? computer : sheet.card === "servers" ? server : null
        }
        // Unless the card has its own button there.
        TextLink {
            objectName: "openServices"
            visible: !(card.item as ComputerSection)?.leadsToPage
            text: sheet.card === "computer" ? Schema.row("speechLocalModel").actionLabel : Words.openServices
            onActivated: sheet.openPage()
        }
    }

    // A sign-in through CLI Proxy API brings the rows both services share.
    Component {
        id: signIn
        Column {
            spacing: Theme.sectionGap
            SignInCard { id: signInCard; width: parent.width; serviceId: sheet.card === "chatgpt" ? "openai" : "anthropic" }
            Reveal {
                open: signInCard.cliproxy
                CliproxySection {}
            }
        }
    }
    Component {
        id: computer
        ComputerSection {
            part: sheet.role
            onPageRequested: sheet.openPage()
        }
    }
    Component {
        id: server
        ServerSection { role: sheet.role }
    }
}
