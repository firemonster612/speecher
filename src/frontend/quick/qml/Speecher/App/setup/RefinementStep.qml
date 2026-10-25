import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App
import "../pages/text"

// A spoken sentence marked up the way refinement edits it, like a proof:
// removed words struck through, added ones in blue pencil. Then the
// providers to do it, or none, which shows the sentence as it was said, and
// under them what a Local Runner or a Custom Endpoint needs.
StepPage {
    id: page

    Plate {
        height: sample.implicitHeight + Theme.space4 * 2
        DiffText {
            id: sample
            x: Theme.space6
            width: parent.width - x * 2
            anchors.verticalCenter: parent.verticalCenter
            segments: ModelText.refinementSample
            mode: page.choices.cleanup === "none" ? "spoken" : "marked"
            variant: "transcript"
        }
    }

    Card {
        ServiceCards {
            options: States.withTones(ModelText.refinementChoices(Models.refinementProviders))
            currentId: page.choices.cleanup
            unratedText: ModelText.unrated
            // One line of help, so every choice fits the step at its
            // default size.
            helpLines: 1
            onActivated: id => page.choices.cleanup = id
        }
        Reveal {
            id: runner
            open: page.choices.cleanup === "local"
            LocalRunnerRows {}
        }
        Reveal {
            id: endpoint
            open: page.choices.cleanup === "endpoint"
            RefinementEndpointRows {}
        }
        // A runner on this computer, offered as a fallback.
        Reveal {
            id: offer
            objectName: "fallbackOffer"
            readonly property var local: Models.fallbackOffers.refinement
            open: (local.joins || []).indexOf(page.choices.cleanup) >= 0
            SettingRow {
                title: ModelText.fallbackOfferText(offer.local)
                Button {
                    text: ModelText.addFallback
                    size: "small"
                    onClicked: Models.acceptFallbackOffer("refinement", page.choices.cleanup)
                }
            }
        }
    }

    Component.onCompleted: Models.checkComputer()
}
