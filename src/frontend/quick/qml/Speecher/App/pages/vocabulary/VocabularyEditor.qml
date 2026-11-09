pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The one editor for every kind of Vocabulary Entry, adding or editing:
// what you say, and what to write instead, which left empty keeps a
// spelling with its Key term and context; then the profiles and apps it is
// limited to; and, editing, whether it is used and what a correction was
// learned from. Add app asks the page for the Apps picker (pickApp); the
// page hands the App it picked to addApp.
InlineForm {
    id: form

    signal pickApp()

    readonly property var t: Schema.vocabulary.t
    // The entry being edited, as Library.vocabulary lists it, or null while
    // adding.
    property var entry: null
    property var draftProfiles: []
    property var draftApps: []
    readonly property bool spelling: write.text.trim().length === 0

    function startAdd() {
        entry = null
        say.text = ""; write.text = ""; context.text = ""; keyTerm.checked = true; use.checked = true
        draftProfiles = []; draftApps = []
        error = ""; open = true
        say.forceActiveFocus()
    }
    function startEdit(e) {
        entry = e
        say.text = e.term; write.text = e.replacement; context.text = e.context; keyTerm.checked = e.keyTerm
        use.checked = e.enabled
        draftProfiles = e.profiles.slice(); draftApps = e.apps.slice()
        error = ""; open = true
    }
    function addApp(id) { if (!draftApps.includes(id)) draftApps = draftApps.concat([id]) }
    function _toggle(list, id) { return list.includes(id) ? list.filter(i => i !== id) : list.concat([id]) }

    title: entry ? t.editEntry : t.newEntry
    saveText: entry ? Words.save : t.add
    cancelText: Words.cancel
    note: Words.savesOnPress(saveText)
    canSave: say.text.trim().length > 0
    onCancelled: open = false
    onSaved: {
        const item = { term: say.text.trim(), replacement: write.text, keyTerm: keyTerm.checked,
                       context: spelling ? context.text.trim() : "", profiles: draftProfiles, apps: draftApps, enabled: use.checked }
        const refused = entry ? Library.update("vocabulary", entry.id, item) : Library.add("vocabulary", item)
        error = Library.refusalText("vocabulary", refused)
        if (!refused) open = false
    }

    Row {
        width: parent.width
        spacing: Theme.space4
        FormField {
            width: (parent.width - parent.spacing) * 0.4
            label: form.t.whenYouSay
            TextField { id: say; objectName: "vocabularySay"; width: parent.width; iconName: "mic" }
        }
        FormField {
            width: (parent.width - parent.spacing) * 0.6
            label: form.t.write
            help: form.t.writeHelp
            Column {
                width: parent.width
                spacing: Theme.space2
                TextArea { id: write; objectName: "vocabularyWrite"; width: parent.width; mono: true; minHeight: 56; placeholderText: form.t.replacementPlaceholder }
                Row {
                    spacing: 6
                    Chip { text: "{date}"; iconName: "calendar"; onClicked: write.insert(write.cursorPosition, "{date}") }
                    Chip { text: "{time}"; iconName: "clock"; onClicked: write.insert(write.cursorPosition, "{time}") }
                }
            }
        }
    }
    // A spelling's own fields.
    Reveal {
        open: form.spelling
        width: parent.width
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField {
                label: Schema.row("vocabularyKeyTerm").label
                help: Schema.row("vocabularyKeyTerm").help
                width: (parent.width - parent.spacing) * 0.4
                Switch { id: keyTerm }
            }
            FormField {
                width: (parent.width - parent.spacing) * 0.6
                label: Schema.row("vocabularyContext").label
                help: Schema.row("vocabularyContext").help
                TextArea { id: context; width: parent.width; minHeight: 56; placeholderText: Schema.row("vocabularyContext").placeholder }
            }
        }
    }
    FormField {
        label: Schema.row("vocabularyProfiles").label
        help: Schema.row("vocabularyProfiles").help + " " + form.t.addAppSaves
        Column {
            width: parent.width
            spacing: Theme.space2
            Flow {
                width: parent.width
                spacing: 6
                Chip { text: form.t.everyProfile; checkable: true; checked: form.draftProfiles.length === 0; onClicked: form.draftProfiles = [] }
                Repeater {
                    model: Library.profiles
                    Chip {
                        required property var modelData
                        text: modelData.name
                        iconName: Library.profileIcon(modelData.id)
                        checkable: true
                        checked: form.draftProfiles.includes(modelData.id)
                        onClicked: form.draftProfiles = form._toggle(form.draftProfiles, modelData.id)
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: 6
                Chip { text: form.t.everyApp; checkable: true; checked: form.draftApps.length === 0; onClicked: form.draftApps = [] }
                Repeater {
                    model: form.draftApps
                    // An app it is limited to; choosing it again takes it off.
                    Chip {
                        required property string modelData
                        text: Library.appName(modelData)
                        iconName: "x"
                        checkable: true
                        checked: true
                        Accessible.name: form.t.removeApp(text)
                        onClicked: form.draftApps = form.draftApps.filter(a => a !== modelData)
                    }
                }
                Chip { objectName: "vocabularyAddApp"; text: form.t.addApp; iconName: "plus"; onClicked: form.pickApp() }
            }
        }
    }
    Reveal {
        open: !!form.entry
        width: parent.width
        FormField {
            width: parent.width
            label: form.t.use
            help: form.entry && form.entry.learned ? Schema.vocabulary.evidenceText(form.entry) + " " + form.t.useHelp : form.t.useHelp
            Switch { id: use }
        }
    }
}
