import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// This computer's part for a job, in a Fix sheet: the Local Models
// downloaded for speech to text, with the This computer page a click away,
// or the Local Runner cleanup runs on. Which model each runs is its order's
// choice.
Section {
    id: section

    // "speech" or "refinement".
    property string part: "speech"

    title: Schema.page("models").title

    SchemaRow {
        id: localRow
        readonly property bool none: Models.downloaded.length === 0
        rowId: "speechLocalModel"
        // A build without local speech says so on This computer.
        shown: section.part === "speech" && ModelText.localSpeech
        help: none ? localRow.spec.emptyHelp
                   : localRow.spec.help.arg(Words.list(Models.downloaded.map(id => (ModelText.catalogModel(id) || { name: id }).name)))
        Button {
            objectName: "openLocalModels"
            text: localRow.spec.actionLabel
            variant: localRow.none ? "primary" : "secondary"
            size: localRow.none ? "medium" : "small"
            onClicked: Navigation.follow("models")
        }
    }
    Reveal {
        open: section.part === "refinement"
        LocalRunnerRows {}
    }
}
