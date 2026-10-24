import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// A spoken sentence marked up the way refinement edits it, like a proof:
// removed words struck through, added ones in blue pencil. Then the
// providers to do it, or none, which shows the sentence as it was said.
StepPage {
    id: page

    Plate {
        height: sample.implicitHeight + Theme.space5 * 2
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
        ChoiceRows {
            recommendedText: Words.recommended
            options: States.withTones(ModelText.refinementChoices(Models.refinementProviders))
            currentId: page.choices.cleanup
            ruleBelow: offer.open
            onActivated: id => page.choices.cleanup = id
        }
        // A runner on this computer, offered as a fallback.
        Reveal {
            id: offer
            readonly property var local: Models.fallbackOffers.refinement
            open: !!local.name && page.choices.cleanup !== "none" && page.choices.cleanup !== "local"
                  && (Settings.values.refinementFallbacks || []).indexOf("local") < 0
            SettingRow {
                title: ModelText.fallbackOfferText(offer.local)
                Button { text: ModelText.addFallback; size: "small"; onClicked: Models.acceptFallbackOffer("refinement") }
            }
        }
    }

    Component.onCompleted: Models.checkComputer()
}
