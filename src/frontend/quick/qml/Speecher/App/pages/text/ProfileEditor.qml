pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One Writing Profile's settings: where it's used, its cleanup level with a
// preview of what that level does, its tone, output language, instructions,
// and the services it may pick for itself. Edits go out through edited();
// the page writes them.
Column {
    id: editor

    property var profile: ({})
    property bool isFallback: false
    property var apps: []
    // Why the page refused the last edit, under the name; empty after one it saved.
    property string refusal
    signal edited(var patch)
    signal deleteRequested()
    signal newToneRequested()

    readonly property var levels: Library.levelOptions
    readonly property var level: levels.find(l => l.id === profile.cleanup) || levels[2]
    readonly property var tones: Library.toneOptions
    readonly property var tone: tones.find(t => t.id === profile.tone) || tones[0]
    // None with an output language still translates, at Light.
    readonly property bool skipsRefinement: profile.cleanup === "none" && !profile.language
    readonly property var refinementModels: profile.refinement === "openai" ? Models.openAiModels
                                          : profile.refinement === "anthropic" ? Models.anthropicModels : []
    property bool confirmingDelete: false

    spacing: Theme.space6

    // Each newly chosen profile fades and slides in; the editor is the same
    // item throughout, so this is the whole change between profiles. The
    // first profile is simply there, and a switch while one is still coming
    // in carries on from where it is rather than starting over.
    property string _shownId: profile.id || ""
    onProfileChanged: {
        confirmingDelete = false
        if (profile.id === _shownId) return
        refusal = ""
        _shownId = profile.id
        if (!swapIn.running) { opacity = 0.2; slide.x = 10 * Theme.movement }
        swapIn.restart()
    }
    transform: Translate { id: slide }
    ParallelAnimation {
        id: swapIn
        Ease { target: editor; property: "opacity"; to: 1; duration: Theme.slow }
        Ease { target: slide; property: "x"; to: 0; duration: Theme.page }
    }

    readonly property string usedIn: Library.profileUse(apps, isFallback)

    // Header: the name (editable on your own profiles), where it's used, and
    // the question before deleting one.
    Column {
        id: header
        width: parent.width
        SearchLanding { rowIds: ["profileApps"]; target: header }
        Item {
            width: parent.width
            height: headCol.implicitHeight
            Column {
                id: headCol
                width: parent.width - deleteButton.width - Theme.space4
                spacing: Theme.space2
                Txt {
                    visible: editor.profile.builtIn
                    text: editor.profile.name || ""
                    variant: "title"
                }
                TextField {
                    visible: !editor.profile.builtIn
                    width: Math.min(parent.width, 300)
                    variant: "title"
                    text: editor.profile.name || ""
                    // Back to the saved name, which a blank or taken one never replaces.
                    onEditingFinished: { editor.edited({ name: text }); text = Qt.binding(() => editor.profile.name || "") }
                }
                Txt { visible: text.length > 0; text: editor.refusal; variant: "small"; tone: "negative"; width: parent.width; wrapMode: Text.WordWrap }
                Txt { text: editor.usedIn; variant: "small"; tone: "secondary"; width: parent.width; wrapMode: Text.WordWrap }
            }
            Button {
                id: deleteButton
                anchors.right: parent.right
                visible: !editor.profile.builtIn
                text: Words.delete_
                iconName: "trash-2"
                variant: "danger"
                size: "small"
                enabled: !editor.confirmingDelete
                onClicked: editor.confirmingDelete = true
            }
        }
        InlineConfirm {
            cancelText: Words.cancel
            open: editor.confirmingDelete
            inset: 0
            ruled: false
            text: Library.profileDeleteQuestion(editor.profile.name)
            detail: Library.profileDeleteDetail(editor.apps, editor.isFallback)
            confirmText: Words.delete_
            onConfirmed: editor.deleteRequested()
            onCancelled: editor.confirmingDelete = false
        }
    }

    // Cleanup, with what it does to a sentence.
    FormField {
        id: cleanupField
        label: Schema.row("profileCleanup").label
        SearchLanding { rowIds: ["profileCleanup"]; target: cleanupField }
        help: editor.level.help
        Column {
            width: parent.width
            spacing: Theme.space3
            CompactChoice {
                width: parent.width
                stretch: true
                options: editor.levels
                currentId: editor.level.id
                onChosen: id => editor.edited({ cleanup: id })
            }
            // The level's sample sentence, quoted beside a rule.
            Item {
                width: parent.width
                height: previewText.height + Theme.space2
                Rectangle { width: 2; height: parent.height; radius: 1; color: Theme.borderStrong }
                MorphText {
                    id: previewText
                    x: Theme.space4
                    y: Theme.space1
                    width: parent.width - x
                    wrap: true
                    text: Library.cleanupSamples[editor.level.base || editor.level.id] || ""
                    tone: editor.level.id === "none" ? "secondary" : "primary"
                }
            }
        }
    }

    FormField {
        id: toneField
        label: Schema.row("profileTone").label
        SearchLanding { rowIds: ["profileTone"]; target: toneField }
        help: editor.skipsRefinement ? Library.profilesText.toneSkipped : editor.tone.help
        // One tone, from a list that grows with your own, so a menu. Skipped
        // refinement leaves it editable for later; the help says so.
        Row {
            id: toneRow
            width: parent.width
            spacing: Theme.space2
            Select {
                // Narrows rather than push Add tone out of a narrow editor.
                width: Math.min(implicitWidth, toneRow.width - newTone.width - toneRow.spacing)
                options: editor.tones
                currentId: editor.tone.id
                onChosen: id => editor.edited({ tone: id })
            }
            Button { id: newTone; text: Library.profilesText.addTone; iconName: "plus"; onClicked: editor.newToneRequested() }
        }
    }

    Row {
        id: textFields
        width: parent.width
        spacing: Theme.space4
        SearchLanding { rowIds: ["profileLanguage", "profileInstructions"]; target: textFields }
        FormField {
            width: (parent.width - parent.spacing) * 0.4
            label: Schema.row("profileLanguage").label
            help: Library.profilesText.languageHelp
            SuggestField {
                suggestionsText: Words.suggestions
                width: parent.width
                iconName: "languages"
                text: editor.profile.language || ""
                placeholderText: Schema.row("profileLanguage").placeholder
                // Empty means the same as spoken, so at rest that reads as
                // the value, as a Select's default does; editing, a hint.
                placeholderTextColor: activeFocus ? Theme.textTertiary : Theme.text
                suggestions: Library.outputLanguages.map(l => ({ id: l, label: l }))
                onCommitted: v => editor.edited({ language: v.trim() })
            }
        }
        FormField {
            width: (parent.width - parent.spacing) * 0.6
            label: Schema.row("profileInstructions").label
            help: Schema.row("profileInstructions").help
            TextArea {
                objectName: "profileInstructions"
                width: parent.width
                minHeight: 64
                text: editor.profile.instructions || ""
                placeholderText: Schema.row("profileInstructions").placeholder
                onEditingFinished: editor.edited({ instructions: text })
            }
        }
    }

    // A profile's own services, which few people set. Its rows bring their
    // own inset, so it reaches out by as much to line up.
    Disclosure {
        id: services
        objectName: "profileServices"
        x: -Theme.rowInset
        width: parent.width + Theme.rowInset * 2
        readonly property bool custom: !!(editor.profile.speech || editor.profile.refinement)
        title: Schema.row("profileServices").label
        help: Schema.row("profileServices").help
        trailingText: custom ? Library.profilesText.custom : Library.profilesText.default_
        open: custom
        SearchLanding {
            rowIds: ["profileServices"]
            target: services
            onLanded: { services.open = true; services.highlight() }
        }

        SettingRow {
            title: Library.profilesText.speech
            help: editor.profile.speech ? Library.profilesText.ownSpeechHelp : Library.profilesText.defaultSpeechHelp
            Select {
                options: [{ id: "", label: Library.defaultChoice(Models.service(Settings.values.speechProvider).label) }]
                         .concat(Models.speechServices.map(s => ({ id: s.id, label: s.label })))
                currentId: editor.profile.speech || ""
                onChosen: id => editor.edited({ speech: id, speechModel: "" })
            }
        }
        SettingRow {
            title: Library.profilesText.speechModel
            shown: editor.profile.speech === "local"
            Select {
                options: [{ id: "", label: Library.defaultChoice((ModelText.catalogModel(Models.localModelChoice) || { name: "" }).name) }]
                         .concat(ModelText.downloadedModels)
                currentId: editor.profile.speechModel || ""
                onChosen: id => editor.edited({ speechModel: id })
            }
        }
        SettingRow {
            title: Library.profilesText.refinement
            help: editor.skipsRefinement ? Library.profilesText.refinementSkipped
                : editor.profile.refinement ? Library.ownRefinementHelp(ModelText.provider(editor.profile.refinement).label)
                : Library.profilesText.defaultRefinementHelp
            Select {
                enabled: !editor.skipsRefinement
                options: [{ id: "", label: Library.defaultChoice(ModelText.provider(Settings.values.refinementProvider).label) }]
                         .concat(Models.refinementProviders.map(p => ({ id: p.id, label: p.label })))
                currentId: editor.profile.refinement || ""
                onChosen: id => editor.edited({ refinement: id, refinementModel: "" })
            }
        }
        SettingRow {
            title: Library.profilesText.refinementModel
            shown: editor.refinementModels.length > 0
            enabled: !editor.skipsRefinement
            Select {
                options: [{ id: "", label: Library.profilesText.defaultModel }].concat(editor.refinementModels.map(m => ({ id: m.id, label: m.label })))
                currentId: editor.profile.refinementModel || ""
                onChosen: id => editor.edited({ refinementModel: id })
            }
        }
    }
}
