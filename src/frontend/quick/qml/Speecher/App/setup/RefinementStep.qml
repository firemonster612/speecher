import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// A spoken sentence marked up the way refinement edits it, like a proof:
// removed words struck through, added ones in blue pencil. Then the
// providers to do it, or none, which shows the sentence as it was said.
StepPage {
    id: page

    Plate {
        height: sample.implicitHeight + Theme.space6 * 2
        DiffText {
            id: sample
            x: Theme.space6
            width: parent.width - x * 2
            anchors.verticalCenter: parent.verticalCenter
            segments: Library.refinementSample
            mode: page.choices.cleanup === "none" ? "spoken" : "marked"
            variant: "transcript"
        }
    }

    Card {
        ChoiceRows {
            recommendedText: Words.recommended
            options: Library.providerChoices
            currentId: page.choices.cleanup
            onActivated: id => page.choices.cleanup = id
        }
    }
}
