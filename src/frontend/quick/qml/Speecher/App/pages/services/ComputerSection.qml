import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// This computer on Services, and in a Fix sheet: the Local Models
// downloaded for speech to text, with Local models a click away, and the
// Local Runner cleanup runs on. Which model each runs is its order's choice.
// A sheet shows only the part that fixes its service.
Section {
    id: section

    // "" for both, or "speech" or "refinement" for one.
    property string part: ""

    title: Schema.row("speechLocalModel").section

    SchemaRow {
        id: localRow
        readonly property bool none: Models.downloaded.length === 0
        rowId: "speechLocalModel"
        // A build without local speech says so on Local models.
        shown: section.part !== "refinement" && ModelText.localSpeech
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
        open: section.part !== "speech"
        LocalRunnerRows {}
    }
}
