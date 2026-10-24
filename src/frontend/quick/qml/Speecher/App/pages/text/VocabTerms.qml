pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The vocabulary terms on one card: the add or edit form, a search with the
// key-term limit beside it, then each term with its context, the profiles it
// is limited to and its use count. Key terms carry a small mic. The page
// holds the import and add buttons.
Card {
    id: view

    SearchLanding {
        rowIds: ["vocabularyEntries", "vocabularyKeyTerm", "vocabularyContext", "vocabularyProfiles", "vocabularyLimit"]
        target: view
    }

    property string startForm   // "add", "edit" or "import", for previews

    readonly property var terms: Library.terms
    // The terms the search matches, each with its place in the full list.
    readonly property var matching: terms.map((t, i) => Object.assign({ index: i }, t))
                                         .filter(t => !search.text || t.term.toLowerCase().includes(search.text.toLowerCase()))
    readonly property int keyTerms: terms.filter(t => t.keyTerm).length


    // Add and edit share one form.
    property int editing: -1
    property var draftProfiles: []
    function startAdd() {
        editing = -1; termName.text = ""; termContext.text = ""; keyTerm.checked = true; draftProfiles = []
        importForm.open = false; form.open = true
    }
    function startEdit(i) {
        const t = terms[i]
        editing = i; termName.text = t.term; termContext.text = t.context; keyTerm.checked = t.keyTerm; draftProfiles = t.profiles.slice()
        importForm.open = false; form.open = true
    }
    function startImport() { form.open = false; importForm.start() }
    function toggleProfile(id) {
        draftProfiles = draftProfiles.includes(id) ? draftProfiles.filter(p => p !== id) : draftProfiles.concat([id])
    }
    Component.onCompleted: if (startForm === "add") startAdd(); else if (startForm === "edit" && terms.length) startEdit(1); else if (startForm === "import") startImport()

    VocabImport { id: importForm; list: "terms" }
    InlineForm {
        cancelText: Words.cancel
        id: form
        title: view.editing < 0 ? Library.vocabularyText.newTerm : Library.vocabularyText.editTerm
        saveText: view.editing < 0 ? Library.vocabularyText.addTerm : Words.save
        canSave: termName.text.trim().length > 0
        onCancelled: open = false
        onSaved: {
            const term = { term: termName.text.trim(), context: termContext.text.trim(), keyTerm: keyTerm.checked, profiles: view.draftProfiles }
            if (view.editing < 0 ? Library.add("terms", term) : Library.update("terms", view.editing, term))
                open = false
        }
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField { width: (parent.width - parent.spacing) / 2; label: Library.vocabularyText.term; TextField { id: termName; width: parent.width } }
            FormField {
                width: (parent.width - parent.spacing) / 2
                label: Schema.row("vocabularyKeyTerm").label
                help: Schema.row("vocabularyKeyTerm").help
                Switch { id: keyTerm }
            }
        }
        FormField {
            label: Schema.row("vocabularyContext").label
            help: Schema.row("vocabularyContext").help
            TextArea { id: termContext; width: parent.width; minHeight: 56; placeholderText: Schema.row("vocabularyContext").placeholder }
        }
        FormField {
            label: Schema.row("vocabularyProfiles").label
            help: Schema.row("vocabularyProfiles").help
            Flow {
                width: parent.width
                spacing: 6
                Chip { text: Library.vocabularyText.everyProfile; checkable: true; checked: view.draftProfiles.length === 0; onClicked: view.draftProfiles = [] }
                Repeater {
                    model: Library.profiles
                    Chip {
                        required property var modelData
                        text: modelData.name
                        iconName: Library.profileIcon(modelData.id)
                        checkable: true
                        checked: view.draftProfiles.includes(modelData.id)
                        onClicked: view.toggleProfile(modelData.id)
                    }
                }
            }
        }
    }

    // Search, and the key-term limit for the current speech service.
    Reveal {
        open: view.terms.length > 0
        Item {
            width: parent.width
            height: Theme.rowHeight + Theme.space1
            SearchField { clearText: Words.clear; id: search; x: Theme.rowInset; anchors.verticalCenter: parent.verticalCenter; placeholderText: Library.vocabularyText.searchTerms }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.rowInset
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3
                Txt {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, view.width - search.width - limit.width - Theme.rowInset * 2 - Theme.space3 * 3)
                    text: Library.keyTermsText
                    variant: "small"
                    tone: "secondary"
                    elide: Text.ElideRight
                }
                ProgressBar {
                    id: limit
                    anchors.verticalCenter: parent.verticalCenter
                    width: 100
                    height: 4
                    value: view.keyTerms / Library.keyTermLimit
                    color: Theme.textSecondary
                }
            }
        }
    }

    RowList {
        id: list
        values: view.matching
        keyOf: t => t.term
        delegate: ListRow {
            id: row
            required property string key
            required property int index
            readonly property var term: list.entry(key)
            width: ListView.view.width
            first: index === 0 && list.first
            height: Math.max(Theme.rowHeight, main.implicitHeight + Theme.space6)

            Column {
                id: main
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                spacing: 3
                Row {
                    spacing: 7
                    Txt { text: row.term.term; variant: "transcript"; strong: true }
                    Icon {
                        visible: row.term.keyTerm
                        name: "mic"
                        size: 13
                        color: Theme.textTertiary
                        anchors.verticalCenter: parent.verticalCenter
                        Tip { text: Library.vocabularyText.keyTermTip; shown: tipHover.hovered }
                        HoverHandler { id: tipHover }
                    }
                }
                Txt {
                    visible: text.length > 0
                    text: Library.termDetail(row.term)
                    variant: "small"
                    tone: "secondary"
                    width: parent.width
                    elide: Text.ElideRight
                }
            }
            trailing: Row {
                spacing: 14
                Txt {
                    visible: row.term.profiles.length > 0
                    text: Library.termProfilesText(row.term.profiles)
                    variant: "small"
                    tone: "secondary"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Txt {
                    text: Library.termUsesText(row.term.uses)
                    variant: "small"
                    tone: "tertiary"
                    width: 58
                    horizontalAlignment: Text.AlignRight
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            actions: [
                IconButton { iconName: "pencil"; size: 28; tooltip: Words.edit; onClicked: view.startEdit(row.term.index) },
                IconButton { iconName: "trash-2"; size: 28; tooltip: Words.delete_; onClicked: { const name = row.term.term; const undo = Library.remove("terms", row.term.index); Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) }) } }
            ]
        }
    }

    Reveal {
        open: view.terms.length > 0 && view.matching.length === 0
        EmptyState {
            compact: true
            title: Library.vocabularyText.noTermsMatch
            text: Library.vocabularyText.noTermsMatchHelp
        }
    }
    Reveal {
        open: view.terms.length === 0 && !form.open && !importForm.open
        EmptyState {
            compact: true
            title: Library.vocabularyText.noTerms
            text: Library.vocabularyText.noTermsHelp
        }
    }
}
