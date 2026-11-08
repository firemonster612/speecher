import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/services"

// A spoken sentence marked up the way refinement edits it, like a proof:
// removed words struck through, added ones in blue pencil. Then the
// providers to do it, or none, which shows the sentence as it was said, and
// under them what a Local Runner or your server needs.
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
            onActivated: id => page.choices.cleanup = id
        }
        // What the chosen service still needs, with its Fix.
        ServiceStatus { role: "refinement"; serviceId: page.choices.cleanup }
        // A provider that can't clean up yet says what that means; setup
        // goes on with it chosen.
        SettingRow {
            readonly property var provider: ModelText.provider(page.choices.cleanup)
            shown: page.choices.cleanup !== "none" && provider.state !== "ready"
            title: Setup.text.cleanupNotReady
            help: Setup.text.cleanupNotReadyHelp
        }
        Reveal {
            id: runner
            open: page.choices.cleanup === "local"
            LocalRunnerRows {}
        }
        Reveal {
            id: endpoint
            open: page.choices.cleanup === "endpoint"
            RefinementServerRows {}
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

    // Refinement services' sign-ins come from Accounts' checks, which
    // start the first time anything asks for Accounts.
    Component.onCompleted: { Models.checkComputer(); ModelText.checkUnknownSignIns(); Accounts.services }
}
