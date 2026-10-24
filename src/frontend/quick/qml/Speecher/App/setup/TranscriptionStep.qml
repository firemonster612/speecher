import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/speech"

// The speech services and where each stands, and under them what the chosen
// one still needs: the Local Model to run and its download, which keeps
// going while setup carries on, the custom endpoint's server, or a sign-in
// to check again. It checks the sign-ins when it opens.
StepPage {
    id: page

    Component.onCompleted: {
        Models.checkComputer()
        Models.checkSignIns()
    }

    Card {
        ChoiceRows {
            recommendedText: Words.recommended
            // What each needs, where the settings pages say what it is good at.
            options: States.withTones(ModelText.setupSpeechChoices(Models.speechServices))
            currentId: page.choices.speech
            ruleBelow: localModel.open || endpoint.open || signIn.open || offer.open
            onActivated: id => page.choices.speech = id
        }
        // The model Local Model runs, any in the catalog, and its download
        // until it is on this computer.
        Reveal {
            id: localModel
            readonly property var model: ModelText.catalogModel(Models.localModelChoice)
            readonly property bool downloading: !!model && Models.downloads[model.id] !== undefined
            open: page.choices.speech === "local" && !!model
            SettingRow {
                title: Schema.row("speechLocalModel").label
                help: localModel.model ? ModelText.metaLine(localModel.model) : ""
                Select {
                    placeholder: Words.choose
                    options: Models.catalog.map(m => ({ id: m.id, label: m.name,
                                                        help: m.id === Models.suggestedModel ? ModelText.suggested : m.bestFor }))
                    currentId: Models.localModelChoice
                    onChosen: id => Settings.set("speechLocalModel", id)
                }
            }
            SettingRow {
                objectName: "localModelDownload"
                shown: !!localModel.model && Models.downloaded.indexOf(localModel.model.id) < 0
                title: Setup.text.downloadModel
                help: localModel.model && localModel.model.fit === "tooLarge" ? ModelText.tooLarge + "." : Setup.text.downloadModelHelp
                Switcher {
                    width: implicitWidth
                    currentIndex: localModel.downloading ? 1 : 0
                    Button {
                        text: localModel.model ? ModelText.download(localModel.model.sizeBytes) : ""
                        variant: "primary"
                        size: "small"
                        enabled: !!localModel.model && localModel.model.fit !== "tooLarge"
                        onClicked: Models.download(localModel.model.id)
                    }
                    Row {
                        spacing: Theme.space2
                        ProgressBar {
                            width: 120
                            value: localModel.downloading ? Models.downloads[localModel.model.id] : 0
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        IconButton {
                            size: 22
                            iconName: "x"
                            tooltip: Words.cancel
                            onClicked: Models.cancel(localModel.model.id)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
        Reveal {
            id: endpoint
            open: page.choices.speech === "endpoint"
            SpeechEndpointRows {}
        }
        // The Local Model this computer can run, offered as a fallback.
        Reveal {
            id: offer
            readonly property var local: Models.fallbackOffers.speech
            open: (local.joins || []).indexOf(page.choices.speech) >= 0
            SettingRow {
                title: ModelText.fallbackOfferText(offer.local)
                Button {
                    text: ModelText.addFallback
                    size: "small"
                    onClicked: Models.acceptFallbackOffer("speech", page.choices.speech)
                }
            }
        }
        // The sign-in the chosen service uses, as core last found it, or why
        // the last check couldn't reach the service.
        Reveal {
            id: signIn
            readonly property var service: ModelText.service(page.choices.speech)
            readonly property bool checking: !!service.signInChecking
            readonly property string failure: service.signInCheckFailure || ""
            open: !!service.signsIn && service.state !== "ready"
            SettingRow {
                title: signIn.service.label
                help: signIn.service.access
                Row {
                    spacing: Theme.space3
                    StatusText {
                        text: signIn.checking ? ModelText.checking : signIn.failure || ModelText.status(signIn.service)
                        busy: signIn.checking
                        tone: !signIn.checking && signIn.failure ? "warning" : States.tone(signIn.service.state)
                        indicator: "dot"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        text: Words.checkAgain
                        size: "small"
                        onClicked: Models.checkSignIns()
                    }
                }
            }
        }
    }
}
