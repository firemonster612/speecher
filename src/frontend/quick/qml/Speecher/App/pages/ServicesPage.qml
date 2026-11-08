pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "services"

// The services Speecher can use, each connected once: ChatGPT and Claude
// side by side with their sign-ins, This computer's Local Model and Local
// Runner, and Your servers, one for speech and one for refinement. Speech to
// text and Refinement put them in order; each order row's Fix opens the
// same card in a sheet.
Page {
    id: page
    title: Schema.page("services").title
    subtitle: Schema.page("services").subtitle
    headerActions: [
        Button {
            text: Words.checkAll
            iconName: "refresh-cw"
            onClicked: Accounts.services.forEach(s => Accounts.recheck(s.id))
        }
    ]

    Component.onCompleted: Models.checkComputer()

    // The two cards end together, and their rows start together; rows that
    // come and go with the sign-in source ease both cards' height.
    Row {
        id: pair
        width: parent.width
        spacing: Theme.space4
        readonly property real _cardWidth: (width - spacing) / 2
        readonly property real _cardHeight: Math.max(openAi.implicitHeight, anthropic.implicitHeight)

        SignInCard {
            id: openAi
            objectName: "chatgptCard"
            serviceId: "openai"
            width: pair._cardWidth
            height: pair._cardHeight
            minimumHeadHeight: anthropic.naturalHeadHeight
        }
        SignInCard {
            id: anthropic
            objectName: "claudeCard"
            serviceId: "anthropic"
            width: pair._cardWidth
            height: pair._cardHeight
            minimumHeadHeight: openAi.naturalHeadHeight
        }
    }
    // CLI Proxy API's rows show in the card that signs in through it; a
    // search for one while neither does lands on the sign-ins.
    SearchLanding {
        rowIds: Settings.values.openAiAuthMode === Accounts.authModes.openAi.cliproxy
                || Settings.values.anthropicAuthMode === Accounts.authModes.anthropic.cliproxy
                ? [] : ["cliproxyOauthDir", "cliproxyBaseUrl", "cliproxyApiKey"]
        target: pair
    }

    ComputerSection { objectName: "thisComputer" }

    // Your servers: one for speech, one for refinement.
    ServerSection { role: "speech" }
    ServerSection { role: "refinement" }

    // Local models says when this build can't run them.
    SubpageRows {
        pageId: "services"
        lines: ModelText.localSpeech ? ({}) : ({ models: Schema.row("localModelBrowser").notInBuild + "." })
    }
}
