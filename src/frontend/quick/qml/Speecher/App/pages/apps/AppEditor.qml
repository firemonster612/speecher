pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../text"

// One App's or one App Type's settings, as rows on a wash under its row:
// for an App what it is recognized by (with Edit), its type, writing
// profile, Pasting, paste keys and History, each starting "Same as …" until
// changed, the vocabulary limited to it, and Remove app; for an App Type its
// writing profile, Pasting, paste keys and History. Pasting is "Same as …"
// or its own steps, the Pasting page's step rows with their help, needs and
// fixes, the paste keys on Press paste keys. Every change saves at once
// through Apps, as the editor says; a refusal shows under the rows.
Item {
    id: editor

    // An entry of Apps.entries, or of Apps.types for an App Type.
    property var entry: ({})
    property bool isType: false
    property string refusal

    readonly property var t: Schema.apps.t
    readonly property bool isOther: isType && entry.id === "general"
    readonly property string otherProfile: (Apps.types.find(o => o.id === "general") || { profile: "" }).profile
    // The type that decides what an App follows.
    readonly property var appType: Schema.apps.type(isType ? entry.id : (entry.effective || {}).type)
    // The Pasting page's steps, as { insert, press, keep }.
    readonly property var pageSteps: System.deliverySteps.reduce((steps, s) => Object.assign(steps, { [s.id]: s.on }), {})
    // What delivering here does: the steps and keys this App or App Type
    // ends up with.
    readonly property var effective: entry.effective || { steps: pageSteps, keys: "standard" }
    readonly property bool terminalWins: isType && Schema.pasting.pastesLikeTerminals(entry.id, Apps.types)

    // Where an App's value would come from without its own, by layer:
    // Terminals, its type, or (fallback) its type following the widest
    // place, which is Other apps for a profile and Pasting for the rest.
    function placeOf(from) {
        return from === "terminal" ? Schema.apps.type("terminal").label : appType.label
    }
    // "Same as …" for each of an App's own values, by where the value would
    // come from without it; an App Type's profile is Other apps' profile.
    function inheritedProfile() {
        if (isType) return Words.sameAs(Schema.apps.otherAppsProfile, Library.profileName(otherProfile))
        const inherited = entry.inherited || {}
        const name = Library.profileName(inherited.profile || "")
        return inherited.profileFrom === "type" ? Words.sameAs(appType.label, name)
             : inherited.profileFrom === "fallback" ? Words.sameAs(Schema.apps.otherAppsProfile, name)
             : t.automatic(name)
    }
    function inheritedSteps() {
        if (isType) return Words.sameAs(t.pastingPage, Schema.pasting.stepsText(pageSteps))
        const inherited = entry.inherited || {}
        return Words.sameAs(placeOf(inherited.stepsFrom), Schema.pasting.stepsText(inherited.steps || pageSteps))
    }
    // The paste keys' choice, on Press's step row or in a row of its own.
    // owner is the editor, whose ids an inline component can't see.
    component KeysSelect: InheritSelect {
        required property var owner
        inheritedText: owner.inheritedKeys()
        choices: Schema.pasting.keyChoices
        currentId: owner.entry.keys || ""
        onChosen: id => owner.save({ keys: id })
    }

    function inheritedKeys() {
        if (isType) return Words.sameAs(t.pastingPage, Schema.pasting.keysLabel("standard"))
        const inherited = entry.inherited || {}
        return Words.sameAs(placeOf(inherited.keysFrom), Schema.pasting.keysLabel(inherited.keys))
    }
    // Turns one of its own steps on or off, keeping one on.
    function setStep(id, on) {
        save({ steps: Object.assign({}, entry.steps, { [id]: on }) })
    }
    // History: what Privacy & data keeps, which an App or App Type can only
    // lower, and the choice this one follows without its own.
    readonly property var keepsSpec: Schema.row("historyKeeps")
    readonly property string keeps: Settings.values.historyKeeps || ""
    readonly property string keepsLabel: (keepsSpec.options.find(o => o.id === keeps) || { label: "" }).label
    readonly property string otherHistory: (Apps.types.find(o => o.id === "general") || { history: "" }).history
    function inheritedHistory() {
        const inherited = entry.inherited || {}
        const fromType = isType ? (isOther ? "" : otherHistory) : inherited.history
        const place = isType ? Schema.apps.type("general").label
                    : inherited.historyFrom === "type" ? appType.label : Schema.apps.type("general").label
        return fromType ? Words.sameAs(place, Schema.apps.historyLabel(fromType))
                        : Words.sameAs(Schema.page(keepsSpec.page).title, keepsLabel)
    }
    readonly property string historyResult: Schema.apps.historyResult(
        isType ? entry.history || (isOther ? "" : otherHistory) : (entry.effective || {}).history, keeps, keepsLabel)
    // The Vocabulary Entries limited to the App, corrections learned in it
    // included.
    readonly property int vocabularyCount: isType ? 0 : Library.vocabulary.filter(e => Library.limitedToApp(e, entry)).length
    function save(patch) {
        refusal = Schema.apps.refusals[isType ? Apps.updateType(entry.id, patch) : Apps.update(entry.id, patch)] || ""
        return !refusal
    }
    // The match an App goes by, worded: an id as it is, text with what it
    // is looked for in.
    function matchText(match) {
        return match.kind === "id" ? match.value
             : (match.kind === "name" ? t.byName : t.byTitle) + " “" + match.value + "”"
    }

    // Editing what the App is recognized by, and its name: a copy of its
    // matches, saved together.
    property bool editing: false
    property var draftMatches: []
    function startEditing() {
        nameField.text = entry.name || ""
        draftMatches = (entry.matches || []).map(m => ({ kind: m.kind, value: m.value }))
        matchForm.error = ""
        editing = true
    }
    function setDraft(index, change) {
        draftMatches = draftMatches.map((m, i) => i === index ? Object.assign({}, m, change) : m)
    }

    width: parent ? parent.width : implicitWidth
    implicitHeight: rows.implicitHeight + Theme.space2 * 2

    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.washInset
        radius: Theme.washRadius
        color: Theme.fill
    }

    Column {
        id: rows
        y: Theme.space2
        width: parent.width

        SettingRow {
            shown: !editor.isType && !editor.editing
            title: editor.t.recognizedAs
            help: [(editor.entry.matches || []).map(m => editor.matchText(m)).join(", "),
                   editor.entry.titleOnly ? editor.t.titleOnlyFollows : "",
                   editor.entry.byText ? editor.t.precedence : ""].filter(s => s).join(" ")
            Button {
                objectName: "appEditMatching"
                text: Words.edit
                size: "small"
                onClicked: editor.startEditing()
            }
        }
        InlineForm {
            id: matchForm
            objectName: "appMatchingForm"
            open: editor.editing
            width: parent.width
            title: editor.t.editMatching
            saveText: Words.save
            cancelText: Words.cancel
            note: Words.savesOnPress(saveText)
            canSave: editor.draftMatches.some(m => m.value.trim().length > 0)
            onCancelled: editor.editing = false
            onSaved: {
                const refused = Apps.update(editor.entry.id, { name: nameField.text, matches: editor.draftMatches })
                error = Schema.apps.refusals[refused] || ""
                if (!refused) editor.editing = false
            }
            FormField {
                label: editor.t.appName
                TextField { id: nameField; width: parent.width }
            }
            FormField {
                label: editor.t.recognizedAs
                help: editor.t.matchKindsHelp
                Column {
                    width: parent.width
                    spacing: Theme.space2
                    // By count, so an edit that keeps the count keeps the
                    // rows: rebuilt, a row's field would lose its focus
                    // after each character typed.
                    Repeater {
                        model: editor.draftMatches.length
                        Row {
                            id: matchRow
                            required property int index
                            readonly property var match: editor.draftMatches[index]
                            width: parent.width
                            spacing: Theme.space2
                            Select {
                                id: kindSelect
                                width: 190
                                options: editor.t.matchKinds
                                currentId: matchRow.match.kind
                                onChosen: id => editor.setDraft(matchRow.index, { kind: id })
                            }
                            TextField {
                                objectName: "appMatchValue_" + matchRow.index
                                width: parent.width - kindSelect.width - removeMatch.width - parent.spacing * 2
                                text: matchRow.match.value
                                onTextEdited: editor.setDraft(matchRow.index, { value: text })
                            }
                            IconButton {
                                id: removeMatch
                                iconName: "x"
                                tooltip: Words.remove
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: editor.draftMatches = editor.draftMatches.filter((m, i) => i !== matchRow.index)
                            }
                        }
                    }
                    Button {
                        text: editor.t.addMatch
                        iconName: "plus"
                        size: "small"
                        variant: "ghost"
                        onClicked: editor.draftMatches = editor.draftMatches.concat([{ kind: "title", value: "" }])
                    }
                }
            }
        }
        SettingRow {
            shown: !editor.isType
            title: editor.t.type
            help: editor.entry.titleOnly ? editor.t.titleOnlyType : ""
            InheritSelect {
                visible: !editor.entry.titleOnly
                inheritedText: editor.t.automatic(Schema.apps.type((editor.entry.inherited || {}).type).one)
                choices: Schema.apps.types.map(o => ({ id: o.id, label: o.label, icon: o.icon }))
                currentId: editor.entry.type || ""
                onChosen: id => editor.save({ type: id })
            }
        }
        SettingRow {
            title: editor.t.profile
            help: editor.isOther ? editor.t.otherProfileHelp : ""
            InheritSelect {
                objectName: "appProfile"
                inheritedText: editor.isOther ? "" : editor.inheritedProfile()
                choices: Library.profileOptions
                currentId: editor.entry.profile || ""
                onChosen: id => editor.save({ profile: id })
            }
        }
        SettingRow {
            title: editor.t.pasting
            help: Schema.pasting.pastingResult(System.stepsPlan(editor.effective.steps), editor.effective.keys, editor.terminalWins)
                + (editor.isType && editor.entry.id === "terminal" ? ". " + editor.t.terminalsNote : "")
            // Choosing its own steps starts from the ones it follows.
            InheritSelect {
                objectName: "appPasting"
                inheritedText: editor.inheritedSteps()
                choices: [Schema.apps.ownSteps]
                currentId: editor.entry.steps ? Schema.apps.ownSteps.id : ""
                onChosen: id => editor.save({ steps: id ? editor.effective.steps : null })
            }
        }
        Reveal {
            open: !!editor.entry.steps
            width: parent.width
            Column {
                width: parent.width
                Repeater {
                    model: Schema.pasting.stepIds
                    DeliveryStepRow {
                        id: stepRow
                        required property string modelData
                        rowId: Schema.pasting.rowOf[modelData]
                        ownSteps: editor.entry.steps || {}
                        keys: editor.effective.keys
                        onStepToggled: on => editor.setStep(modelData, on)
                        KeysSelect {
                            owner: editor
                            visible: stepRow.modelData === "press" && Schema.pasting.keyChoices.length > 0
                            // Narrower than a settings row's, beside the
                            // step's needs and switch.
                            width: 170
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
        // Following another place's steps, the keys have a row of their own.
        SettingRow {
            shown: Schema.pasting.keyChoices.length > 0 && !editor.entry.steps && !!editor.effective.steps.press
            title: editor.t.pasteKeys
            help: editor.t.pasteKeysHelp
            KeysSelect {
                objectName: "appPasteKeys"
                owner: editor
            }
        }
        SettingRow {
            title: editor.t.history
            help: editor.historyResult
            InheritSelect {
                objectName: "appHistory"
                inheritedText: editor.inheritedHistory()
                choices: Schema.apps.historyChoices
                currentId: editor.entry.history || ""
                onChosen: id => editor.save({ history: id })
            }
        }
        SettingRow {
            shown: !editor.isType
            title: Schema.page("vocabulary").title
            help: Schema.vocabulary.limitedHere(editor.vocabularyCount, "app")
            Button {
                objectName: "appVocabulary"
                text: Schema.vocabulary.showThem
                size: "small"
                onClicked: Navigation.follow("vocabulary", "vocabularyFor:app/" + editor.entry.id)
            }
        }
        Reveal {
            open: editor.refusal.length > 0
            Txt { x: Theme.rowInset; width: parent.width - Theme.rowInset * 2; text: editor.refusal; variant: "small"; tone: "negative"; wrapMode: Text.WordWrap }
        }
        Txt {
            x: Theme.rowInset
            width: parent.width - Theme.rowInset * 2
            topPadding: Theme.space2
            bottomPadding: Theme.space2
            text: Words.savesAsYouGo
            variant: "small"
            tone: "tertiary"
        }
        SettingRow {
            shown: !editor.isType
            title: editor.t.remove
            help: editor.t.removeHelp
            Button {
                text: editor.t.remove
                iconName: "trash-2"
                variant: "danger"
                size: "small"
                onClicked: {
                    const name = editor.entry.name
                    const undo = Apps.remove(editor.entry.id)
                    if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Apps.undo(undo) })
                }
            }
        }
    }
}
