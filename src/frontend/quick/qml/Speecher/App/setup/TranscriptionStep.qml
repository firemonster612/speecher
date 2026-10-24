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

    Component.onCompleted: Models.checkComputer()

    Card {
        ChoiceRows {
            recommendedText: Words.recommended
            // What each needs, where the settings pages say what it is good at.
            options: States.withTones(ModelText.setupSpeechChoices(Models.speechServices))
            currentId: page.choices.speech
            ruleBelow: download.open || endpoint.open || signIn.open || offer.open
            onActivated: id => page.choices.speech = id
        }
        Reveal {
            id: download
            readonly property var model: ModelText.catalogModel(Models.localModelChoice)
            open: page.choices.speech === "local" && Models.downloaded.length === 0 && !!model
            SettingRow {
                title: Setup.text.downloadModel
                help: Setup.text.downloadModelHelp
                Button {
                    text: download.model ? ModelText.download(download.model.sizeBytes) : ""
                    variant: "primary"
                    size: "small"
                    enabled: !!download.model && Models.downloads[download.model.id] === undefined
                    onClicked: Models.download(download.model.id)
                }
            }
        }
        Reveal {
            id: endpoint
            open: page.choices.speech === "endpoint"
            SchemaRow { rowId: "speechEndpointUrl" }
        }
        // The Local Model this computer can run, offered as a fallback.
        Reveal {
            id: offer
            readonly property var local: Models.fallbackOffers.speech
            open: !!local.name && page.choices.speech !== "local"
                  && (Settings.values.speechFallbackList || []).indexOf("local") < 0
            SettingRow {
                title: ModelText.fallbackOfferText(offer.local)
                Button { text: ModelText.addFallback; size: "small"; onClicked: Models.acceptFallbackOffer("speech") }
            }
        }
        Reveal {
            id: signIn
            // The sign-in a speech service uses, or null for one that needs none.
            readonly property var account: page.choices.speech === "codex" ? Accounts.service("openai")
                                         : page.choices.speech === "claude" ? Accounts.service("anthropic") : null
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
