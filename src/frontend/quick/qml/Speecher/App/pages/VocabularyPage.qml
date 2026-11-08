pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "apps"
import "vocabulary"

// The Vocabulary as one list: spellings, replacements and Learned
// Corrections, with filters by kind and by what an entry is limited to, a
// search, and the one editor for every kind at the top of the card. Below
// it, the switch that learns corrections. A search for a kind's row lands
// on the list filtered to that kind; "vocab:<id>" opens an entry, and
// "vocabularyFor:profile/<id>" or "vocabularyFor:app/<id>" (from Writing
// profiles and Apps) shows the entries limited to it.
Page {
    id: page
    objectName: "vocabularyPage"
    title: Schema.page("vocabulary").title
    subtitle: Schema.page("vocabulary").subtitle

    readonly property var t: Schema.vocabulary.t
    // The kind on show (Schema.vocabulary.filters) and the limit, "" for any.
    property string filter: "all"
    property string limit: ""
    // The limit a link asked for, offered even before an entry has it.
    property string linkedLimit: ""
    readonly property string query: search.text.trim().toLowerCase()
    readonly property var shown: Library.vocabulary.filter(e => Schema.vocabulary.shows(filter, e)
        && Library.limitedTo(e, limit)
        && (!query || [e.term, e.replacement, e.context].some(s => s.toLowerCase().includes(query))))
    readonly property var limitChoices: Library.limitChoices(linkedLimit)
    readonly property var kindRows: ["vocabularyEntries", "bindingRules", "learnedCorrections", "vocabularyKeyTerm",
                                     "vocabularyContext", "vocabularyProfiles", "vocabularyLimit"]
    // Where a link or search may land: a limit, or one entry.
    readonly property var linkRows: Library.profiles.map(p => "vocabularyFor:profile/" + p.id)
        .concat(Apps.entries.map(a => "vocabularyFor:app/" + a.id))
        .concat(Library.vocabulary.map(e => "vocab:" + e.id))

    function _land(rowId) {
        if (rowId.startsWith("vocabularyFor:")) {
            linkedLimit = limit = rowId.slice("vocabularyFor:".length)
            filter = "all"
            search.text = ""
        } else if (rowId.startsWith("vocab:")) {
            const entry = Library.vocabulary.find(e => "vocab:" + e.id === rowId)
            if (entry) editor.startEdit(entry)
        } else if (kindRows.includes(rowId)) {
            filter = Schema.vocabulary.filterFor(rowId) || "all"
            limit = ""
        }
    }
    // Whether the page has applied the link it opened with, so the list
    // loads with that filter rather than fading the other rows out.
    property bool _opened: false
    Component.onCompleted: { _land(Navigation.pendingRow); _opened = true }
    Connections { target: Navigation; function onPendingRowChanged() { page._land(Navigation.pendingRow) } }

    VocabularyImport { id: importChooser }

    Section {
        id: listSection
        title: page.t.yourVocabulary
        caption: Library.spellings.length ? Library.keyTermsText : Schema.row("vocabularyEntries").help
        actions: [
            Button { text: page.t.import_; iconName: "file-up"; size: "small"; onClicked: { editor.open = false; importChooser.open() } },
            Button { objectName: "vocabularyAdd"; text: page.t.add; iconName: "plus"; size: "small"; onClicked: { picker.close(); editor.startAdd() } }
        ]
        SearchLanding { rowIds: page.kindRows; target: listSection }
        SearchLanding { rowIds: page.linkRows; target: listSection }

        VocabularyEditor {
            id: editor
            objectName: "vocabularyEditor"
            onPickApp: picker.start()
            onOpenChanged: if (!open) picker.close()
        }
        AppPicker {
            id: picker
            onPicked: id => editor.addApp(id)
        }

        // The filters, while there is anything to filter.
        Reveal {
            open: Library.vocabulary.length > 0
            // The search takes the rest of the line, or a line of its own
            // where too little is left.
            Flow {
                id: filters
                x: Theme.rowInset
                width: parent.width - Theme.rowInset * 2
                topPadding: Theme.space3
                bottomPadding: Theme.space3
                spacing: Theme.space3
                SegmentedControl {
                    id: kinds
                    objectName: "vocabularyFilter"
                    options: Schema.vocabulary.filters.map(f => ({ id: f.id, label: f.label }))
                    currentId: page.filter
                    onActivated: id => page.filter = id
                }
                Select {
                    id: limits
                    objectName: "vocabularyLimitFilter"
                    visible: page.limitChoices.length > 0
                    options: [{ id: "", label: page.t.anyLimit }].concat(page.limitChoices)
                    currentId: page.limit
                    onChosen: id => page.limit = id
                }
                SearchField {
                    id: search
                    readonly property real rest: filters.width - kinds.width - (limits.visible ? limits.width + filters.spacing : 0) - filters.spacing
                    width: rest >= 180 ? rest : filters.width
                    clearText: Words.clear
                    placeholderText: page.t.search
                }
            }
        }

        RowList {
            id: list
            values: page._opened ? page.shown : []
            keyOf: e => e.id
            delegate: VocabularyRow {
                required property string key
                required property int index
                width: ListView.view.width
                entry: list.entry(key)
                // The filters sit above every row.
                first: false
                onEditRequested: { picker.close(); editor.startEdit(entry); page.reveal(editor) }
            }
        }

        Reveal {
            open: Library.vocabulary.length === 0 && !editor.open
            EmptyState { compact: true; title: page.t.noEntries; text: page.t.noEntriesHelp }
        }
        Reveal {
            open: Library.vocabulary.length > 0 && page.shown.length === 0
            EmptyState {
                compact: true
                title: page.limit && !page.query ? page.t.noLimited : page.t.noMatch
                text: page.limit && !page.query ? page.t.noLimitedHelp : page.t.noMatchHelp
            }
        }
    }

    Section {
        id: learning
        title: page.t.learning
        caption: page.t.learningHelp
        actions: [
            Button {
                text: page.t.undoCorrection
                iconName: "undo-2"
                size: "small"
                enabled: Library.vocabulary.some(e => e.learned)
                onClicked: {
                    const newest = Library.vocabulary.filter(e => e.learned).sort((a, b) => b.learnedAtMs - a.learnedAtMs)[0]
                    const undo = Library.undoLastCorrection()
                    if (undo >= 0) Toaster.show(Words.deleted(newest.replacement), { icon: "undo-2", actionText: Words.undo, action: () => Library.undo(undo) })
                }
            }
        ]
        AccessibilityNotice { text: page.t.correctionsNeedAccessibility }
        SchemaRow { rowId: "correctionLearningControl" }
    }
}
