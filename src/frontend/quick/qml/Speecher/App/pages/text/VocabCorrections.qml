pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// Learned corrections: the switch that learns them, then each one marked like
// a proof, the word heard struck through in the margin's red and the word
// written in blue pencil, with where it was learned and how sure Speecher is.
// Nothing is added by hand. All on one card under the switch that learns
// them; the page holds the undo button.
Card {
    id: view

    SearchLanding { rowIds: ["learnedCorrections"]; target: view }

    AccessibilityNotice { text: Library.vocabularyText.correctionsNeedAccessibility }
    SchemaRow { rowId: "correctionLearningControl"; enabled: System.accessibility }

    RowList {
        id: corrections
        values: Library.corrections
        delegate: ListRow {
            id: row
            required property string key
            required property int index
            readonly property var correction: corrections.entry(key)
            readonly property real dim: correction.enabled ? 1 : 0.5
            width: ListView.view.width
            first: index === 0 && corrections.first

            DiffText {
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
                segments: [{ kind: "removed", text: row.correction.heard }, { kind: "same", text: " " }, { kind: "added", text: row.correction.corrected }]
                variant: "transcript"
                opacity: row.dim
                Behavior on opacity { Ease {} }
            }
            trailing: Row {
                spacing: Theme.space5
                Row {
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: row.dim
                    Behavior on opacity { Ease {} }
                    AppIcon { name: row.correction.app.split(".").pop(); size: 16; anchors.verticalCenter: parent.verticalCenter }
                    Txt {
                        text: Library.correctionSource(row.correction)
                        variant: "small"
                        tone: "secondary"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Switch {
                    checked: row.correction.enabled
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: Library.update("corrections", row.index, { enabled: checked })
                }
            }
        }
    }

    Reveal {
        open: Library.corrections.length === 0
        EmptyState {
            compact: true
            title: Library.vocabularyText.noCorrections
            text: Library.vocabularyText.noCorrectionsHelp
        }
    }
}
