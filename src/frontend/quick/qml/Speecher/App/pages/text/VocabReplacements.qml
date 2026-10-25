pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Replacements and snippets: a spoken phrase on the left, the exact text it
// becomes on the right, with {date} and {time} picked out. All on one card
// with the add or edit form at the top; the page holds the import and add
// buttons.
Card {
    id: view

    SearchLanding { rowIds: ["bindingRules"]; target: view }

    readonly property var items: Library.replacements

    // The replacement as proof segments, its placeholders marked as added
    // text: they are what Speecher fills in.
    function segments(text) {
        return text.split(/(\{date\}|\{time\})/).filter(t => t).map(t => ({ kind: /^\{(date|time)\}$/.test(t) ? "added" : "same", text: t }))
    }

    // The phrase being edited, as saved, or empty while adding.
    property string editing
    function startAdd() { editing = ""; phrase.text = ""; replacement.text = ""; form.error = ""; form.open = true }
    function startEdit(r) { editing = r.phrase; phrase.text = r.phrase; replacement.text = r.replacement; form.error = ""; form.open = true }
    function startImport() { form.open = false; importChooser.open() }

    VocabImport { id: importChooser; list: "replacements" }
    InlineForm {
        cancelText: Words.cancel
        id: form
        title: view.editing ? Library.vocabularyText.editReplacement : Library.vocabularyText.newReplacement
        saveText: view.editing ? Words.save : Library.vocabularyText.addReplacement
        canSave: phrase.text.trim().length > 0 && replacement.text.length > 0
        onCancelled: open = false
        onSaved: {
            const item = { phrase: phrase.text.trim(), replacement: replacement.text }
            const refused = view.editing ? Library.update("replacements", view.editing, item) : Library.add("replacements", item)
            error = Library.refusalText("replacements", refused)
            if (!refused) open = false
        }
        FormField { label: Library.vocabularyText.whenYouSay; TextField { id: phrase; width: parent.width; iconName: "mic" } }
        FormField {
            label: Library.vocabularyText.speecherWrites
            help: Library.vocabularyText.speecherWritesHelp
            Column {
                width: parent.width
                spacing: Theme.space2
                TextArea { id: replacement; width: parent.width; mono: true; minHeight: 84; placeholderText: Library.vocabularyText.replacementPlaceholder }
                Row {
                    spacing: 6
                    Chip { text: "{date}"; iconName: "calendar"; onClicked: replacement.insert(replacement.cursorPosition, "{date}") }
                    Chip { text: "{time}"; iconName: "clock"; onClicked: replacement.insert(replacement.cursorPosition, "{time}") }
                }
            }
        }
    }

    RowList {
        id: list
        values: view.items
        keyOf: r => r.phrase
        delegate: ListRow {
            id: row
            required property string key
            required property int index
            readonly property var item: list.entry(key)
            readonly property real said: width * 0.32
            width: ListView.view.width
            first: index === 0 && list.first
            height: Math.max(Theme.rowHeight, out.implicitHeight + 40)

            Row {
                y: 19
                width: row.said
                spacing: 7
                Icon { name: "mic"; size: 13; color: Theme.textTertiary; anchors.verticalCenter: parent.verticalCenter }
                Txt { text: row.item.phrase; variant: "label"; elide: Text.ElideRight; width: Math.min(implicitWidth, row.said - 20); anchors.verticalCenter: parent.verticalCenter }
            }
            Icon { x: row.said + Theme.space2; y: 21; name: "arrow-right"; size: 14; color: Theme.textTertiary }
            Rectangle {
                x: row.said + 36
                y: Theme.space3
                width: parent.width - x
                height: out.implicitHeight + Theme.space4
                radius: Theme.radius
                color: Theme.sheet
                DiffText {
                    id: out
                    x: Theme.space3
                    y: Theme.space2
                    width: parent.width - Theme.space3 * 2
                    segments: view.segments(row.item.replacement)
                    variant: "code"
                }
            }
            actions: [
                IconButton { iconName: "pencil"; size: 28; tooltip: Words.edit; onClicked: view.startEdit(row.item) },
                IconButton {
                    iconName: "trash-2"; size: 28; tooltip: Words.delete_
                    onClicked: {
                        const name = row.item.phrase
                        const undo = Library.remove("replacements", name)
                        if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
                    }
                }
            ]
        }
    }

    Reveal {
        open: view.items.length === 0 && !form.open
        EmptyState {
            compact: true
            title: Library.vocabularyText.noReplacements
            text: Library.vocabularyText.noReplacementsHelp
        }
    }
}
