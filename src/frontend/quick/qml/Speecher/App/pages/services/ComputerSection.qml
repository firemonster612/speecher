import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// This computer on Services, and in a Fix sheet: the Local Model speech to
// text runs, with Local models a click away, and the Local Runner cleanup
// runs on. A sheet shows only the part that fixes its service.
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
        help: none ? localRow.spec.emptyHelp : localRow.spec.help
        Row {
            spacing: Theme.space3
            Select {
                visible: !localRow.none
                width: Theme.selectWidth
                placeholder: Words.choose
                options: Models.downloaded.map(id => ModelText.catalogModel(id))
                                          .map(m => ({ id: m.id, label: m.name, help: m.bestFor }))
                currentId: Models.localModelChoice
                onChosen: id => Settings.set("speechLocalModel", id)
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                objectName: "openLocalModels"
                text: localRow.spec.actionLabel
                variant: localRow.none ? "primary" : "secondary"
                size: localRow.none ? "medium" : "small"
                onClicked: Navigation.follow("models")
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    Reveal {
        open: section.part !== "speech"
        LocalRunnerRows {}
    }
}
