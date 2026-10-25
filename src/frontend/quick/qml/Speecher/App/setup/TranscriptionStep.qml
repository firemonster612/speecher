import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The speech services and where each stands, and under them what the chosen
// one still needs: the Local Model's download, which keeps going while setup
// carries on, the custom endpoint's URL, or a sign-in to check again.
StepPage {
    id: page

    Card {
        ChoiceRows {
            recommendedText: Words.recommended
            // What each needs, where the settings pages say what it is good at.
            options: States.withTones(Models.serviceChoices).map(o => Object.assign({}, o, { help: Models.service(o.id).access }))
            currentId: page.choices.speech
            ruleBelow: download.open || endpoint.open || signIn.open
            onActivated: id => page.choices.speech = id
        }
        Reveal {
            id: download
            readonly property var model: Models.catalogModel(Models.localModelChoice)
            open: page.choices.speech === "local" && Models.downloaded.length === 0
            SettingRow {
                title: Setup.text.downloadModel
                help: Setup.text.downloadModelHelp
                Button {
                    text: Models.text.download.arg(download.model.size)
                    variant: "primary"
                    size: "small"
                    enabled: Models.downloads[download.model.id] === undefined
                    onClicked: Models.download(download.model.id)
                }
            }
        }
        Reveal {
            id: endpoint
            open: page.choices.speech === "endpoint"
            SchemaRow { rowId: "speechEndpointUrl" }
        }
        Reveal {
            id: signIn
            readonly property var account: Models.account(page.choices.speech)
            open: !!account && account.state !== "signedIn"
            SettingRow {
                title: signIn.account ? signIn.account.name : ""
                help: signIn.account ? signIn.account.detail : ""
                Row {
                    spacing: Theme.space3
                    StateText {
                        stateId: signIn.account ? signIn.account.state : ""
                        text: signIn.account ? Accounts.stateLabels[signIn.account.state] : ""
                        indicator: "dot"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: Words.checkAgain
                        size: "small"
                        enabled: !!signIn.account && !States.busy(signIn.account.state)
                        onClicked: Accounts.recheck(signIn.account.id)
                    }
                }
            }
        }
    }
}
