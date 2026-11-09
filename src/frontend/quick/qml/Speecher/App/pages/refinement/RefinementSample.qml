import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.App

// What refinement does to a sentence, marked like a proof, leading
// Refinement and its setup step. With no service the marks fade and it
// reads as it will be pasted; under it, who would clean it up, or that
// nothing does.
Section {
    id: section

    readonly property string primary: ModelText.mainRefiner
    readonly property string sampleCaption: {
        if (primary === "none") return "An example, pasted as spoken. Turn on Clean up my words to clean it up."
        const model = ModelText.refinementModel(primary)
        return "An example of cleanup by " + ModelText.provider(primary).label + (model ? " with " + model : "") + "."
    }

    plain: true
    Plate {
        height: sample.implicitHeight + Theme.space4 * 2
        Column {
            id: sample
            x: Theme.space6
            y: Theme.space4
            width: parent.width - Theme.space6 * 2
            spacing: Theme.space3
            DiffText {
                segments: ModelText.refinementSample
                mode: section.primary === "none" ? "spoken" : "marked"
                variant: "lead"
            }
            MorphText {
                text: section.sampleCaption
                variant: "small"
                tone: "secondary"
            }
        }
    }
}
