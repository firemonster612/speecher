pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Learned corrections: the switch that learns them, then each one marked like
// a proof, the word heard struck through in the margin's red and the word
// written in blue pencil, with where it was learned and how sure Speecher is.
// Nothing is added by hand, but each can be corrected or deleted. All on one
// card under the switch that learns them; the page holds the undo button.
Card {
    id: view

    SearchLanding { rowIds: ["learnedCorrections"]; target: view }

    AccessibilityNotice { text: Library.vocabularyText.correctionsNeedAccessibility }
    SchemaRow { rowId: "correctionLearningControl"; enabled: System.accessibility }

    // The correction being edited, by id.
    property string editing
    function startEdit(c) { editing = c.id; heard.text = c.heard; corrected.text = c.corrected; form.error = ""; form.open = true }

    InlineForm {
        id: form
        title: Library.vocabularyText.editCorrection
        saveText: Words.save
        cancelText: Words.cancel
        canSave: heard.text.trim().length > 0 && corrected.text.trim().length > 0
        onCancelled: open = false
        onSaved: {
            const refused = Library.update("corrections", view.editing, { heard: heard.text.trim(), corrected: corrected.text.trim() })
            error = Library.refusalText("corrections", refused)
            if (!refused) open = false
        }
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField { width: (parent.width - parent.spacing) / 2; label: Library.vocabularyText.heard; TextField { id: heard; width: parent.width } }
            FormField { width: (parent.width - parent.spacing) / 2; label: Library.vocabularyText.corrected; TextField { id: corrected; width: parent.width } }
        }
    }

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
                    onToggled: Library.update("corrections", row.correction.id, { enabled: checked })
                }
            }
            actions: [
                IconButton { iconName: "pencil"; size: 28; tooltip: Words.edit; onClicked: view.startEdit(row.correction) },
                IconButton {
                    iconName: "trash-2"; size: 28; tooltip: Words.delete_
                    onClicked: {
                        const name = row.correction.corrected
                        const undo = Library.remove("corrections", row.correction.id)
                        if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
                    }
                }
            ]
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
