pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// One Writing Profile's settings: where it's used and the vocabulary limited
// to it, its cleanup level with a
// preview of what that level does, its tone, output language, the rules it
// turns on, and the services it may pick for itself. Edits go out through
// edited(), ruleSwitched() and rulesReset(); the page writes them.
//
// In run mode it is what one run (a Transcribe batch) changes of its
// profile: only cleanup, tone and the services show, each starting "Same as
// <profile> (…)", and the edits are the run's, by the same keys. A run has
// no rules of its own.
Column {
    id: editor

    // A profile, or in run mode the run's own changes: { cleanup, tone,
    // speech, speechModel, refinement, refinementModel }, each "" while it
    // is the same as the profile's.
    property var profile: ({})
    property bool runMode: false
    // Run mode: each value without the run's change and the place it comes
    // from: { profileName, cleanup, tone, outputLanguage, speech,
    // speechPlace, speechModel, refinement, refinementPlace,
    // refinementModel }.
    property var inherited: ({})
    property string servicesTitle: Schema.row("profileServices").label
    // A search for a profile's rows lands on the Writing profiles page,
    // never on a run's copy of them.
    function landsOn(rowIds) { return runMode ? [] : rowIds }
    // The Apps and App Types that write with it, as Library.profileUsers
    // lists them: [{ id (where it lands in Apps), name, icon, typeIcon }].
    property var apps: []
    // Why the page refused the last edit, under the name; empty after one it saved.
    property string refusal
    signal edited(var patch)
    signal deleteRequested()
    signal ruleSwitched(string rule, bool on)
    signal rulesReset()
    signal newRuleRequested()

    readonly property var levels: Library.cleanupLevels
    readonly property var level: levels.find(l => l.id === (profile.cleanup || inherited.cleanup)) || levels[2]
    readonly property var tones: Library.tones
    readonly property var tone: tones.find(t => t.id === (profile.tone || inherited.tone)) || tones[0]
    readonly property string outputLanguage: runMode ? inherited.outputLanguage || "" : profile.language || ""
    // None with an output language still translates, at Light.
    readonly property bool skipsRefinement: level.id === "none" && !outputLanguage
    readonly property bool translatesOnly: level.id === "none" && !!outputLanguage
    // A custom system prompt stands in for the built-in rules.
    readonly property bool customPrompt: !!Settings.values.customSystemPromptEnabled
                                         && !!(Settings.values.customSystemPrompt || "").trim()
    // What the profile runs: its own first services, else the pages'
    // orders'. A speech service the resolver passes over (it can't listen
    // for the Spoken Language, or this build lacks it) leaves the page's.
    // With Clean up my words off, nothing refines, unless a run names a
    // service.
    readonly property string speechSkipped: runMode ? "" : profile.speechSkipped || ""
    readonly property var speech: ModelText.service((speechSkipped ? "" : profile.speech)
                                                    || (runMode ? inherited.speech : ModelText.mainSpeech))
    readonly property bool refinementOff: !runMode && ModelText.mainRefiner === "none"
    readonly property var refiner: ModelText.provider(refinementOff ? "none"
        : profile.refinement || (runMode ? inherited.refinement : ModelText.mainRefiner))
    readonly property bool refinerOff: refiner.id === "none"
    // Where a service the profile or run leaves alone comes from, and what
    // it is there.
    readonly property string speechPlace: runMode ? inherited.speechPlace : Schema.page("speech").title
    readonly property string refinementPlace: runMode ? inherited.refinementPlace : Schema.page("refinement").title
    readonly property string inheritedSpeech: runMode ? inherited.speech : ModelText.mainSpeech
    readonly property string inheritedRefiner: runMode ? inherited.refinement : ModelText.mainRefiner
    function speechModelName(model) { return (ModelText.catalogModel(model) || { name: model }).name || Library.profilesText.serviceDefault }
    // The services whose models the editor chooses: the profile's own, or
    // in run mode the ones the run hears and refines with.
    readonly property string modelSpeech: runMode ? speech.id : profile.speech || ""
    readonly property string modelRefiner: profile.refinement || (runMode ? refiner.id : "")
    // "Same as …" for a model: the run's inherited service keeps its place's
    // model; any other service runs its page's entry.
    readonly property string inheritedSpeechModel: runMode && speech.id === inheritedSpeech
        ? Words.sameAs(speechPlace, speechModelName(inherited.speechModel))
        : Words.sameAs(Schema.page("speech").title, speechModelName(ModelText.entry("speech", modelSpeech).model || ""))
    readonly property string inheritedRefinementModel: runMode && refiner.id === inheritedRefiner
        ? Words.sameAs(refinementPlace, inherited.refinementModel || Library.profilesText.serviceDefault)
        : Words.sameAs(Schema.page("refinement").title, ModelText.refinementModel(refiner.id) || Library.profilesText.serviceDefault)
    // The language the Spoken Language names, for why a service is skipped.
    readonly property string spokenLanguageName: (Schema.row("spokenLanguage").options.find(o => o.id === Settings.values.spokenLanguage)
                                                  || { label: "" }).label
    // "Heard by Claude, cleaned up by gpt-6-luna.", then what stands in the
    // way of either, so a profile that seems to ignore a change says why.
    readonly property string pipeline: {
        const model = profile.refinementModel || ModelText.refinementModel(refiner.id)
        const used = [speech].concat(skipsRefinement || refinerOff ? [] : [refiner])
        const said = p => { const status = ModelText.status(p); return (status.includes(p.label) ? status : p.label + ": " + status) + "." }
        const problems = used.filter(p => p.state !== "ready").map(said)
        return [Library.pipelineCaption(speech.id, skipsRefinement ? "none" : refiner.id, model) + "."]
            .concat(runMode || profile.speech || profile.refinement ? [] : [Library.profilesText.everyProfile]).concat(problems).join(" ")
    }
    property bool confirmingDelete: false

    spacing: Theme.space6

    // Each newly chosen profile fades and slides in; the editor is the same
    // item throughout, so this is the whole change between profiles. The
    // first profile is simply there, and a switch while one is still coming
    // in carries on from where it is rather than starting over.
    property string _shownId: profile.id || ""
    onProfileChanged: {
        confirmingDelete = false
        // A run's changes are one run's throughout.
        if (runMode || profile.id === _shownId) return
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


    // Header: the name (editable on your own profiles), where it's used, and
    // the question before deleting one.
    Column {
        id: header
        visible: !editor.runMode
        width: parent.width
        SearchLanding { rowIds: editor.landsOn(["profileApps"]); target: header }
        Item {
            width: parent.width
            height: headCol.implicitHeight
            Column {
                id: headCol
                width: parent.width - deleteButton.width - Theme.space4
                spacing: Theme.space2
                Txt {
                    visible: !!editor.profile.builtIn
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
                Reveal {
                    open: editor.refusal.length > 0
                    Txt { text: editor.refusal; variant: "small"; tone: "negative"; width: parent.width; wrapMode: Text.WordWrap }
                }
                // Each app or type that uses it opens in Apps.
                Flow {
                    width: parent.width
                    spacing: Theme.space2
                    Txt {
                        text: editor.apps.length ? Schema.row("profileApps").label : Library.profilesText.unused
                        variant: "small"
                        tone: "secondary"
                    }
                    Repeater {
                        model: editor.apps
                        TextLink {
                            required property var modelData
                            text: modelData.name
                            iconName: modelData.typeIcon
                            onActivated: Navigation.follow("apps", modelData.id)
                        }
                    }
                }
                // The vocabulary limited to it, shown on Vocabulary.
                LinkedText {
                    id: vocabularyLink
                    objectName: "profileVocabulary"
                    readonly property string limit: "profile/" + (editor.profile.id || "")
                    width: parent.width
                    plainText: Schema.vocabulary.limitedHere(Library.limitedCount(limit), "profile") + " " + Schema.vocabulary.showThem
                    link: Schema.vocabulary.showThem
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                    onActivated: Navigation.follow("vocabulary", "vocabularyFor:" + limit)
                }
                Txt { text: Words.savesAsYouGo; variant: "small"; tone: "tertiary" }
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
            detail: Library.profileDeleteDetail(editor.apps)
            confirmText: Words.delete_
            onConfirmed: editor.deleteRequested()
            onCancelled: editor.confirmingDelete = false
        }
    }

    // Cleanup, with what it does to a sentence.
    FormField {
        id: cleanupField
        label: Schema.row("profileCleanup").label
        SearchLanding { rowIds: editor.landsOn(["profileCleanup"]); target: cleanupField }
        help: editor.refinerOff && !editor.skipsRefinement ? Library.profilesText.noRefiner
            : editor.translatesOnly ? Library.translatesAtLight(editor.refiner.label) : editor.level.help
        Column {
            width: parent.width
            spacing: Theme.space3
            CompactChoice {
                visible: !editor.runMode
                width: parent.width
                stretch: true
                options: editor.levels
                currentId: editor.level.id
                onChosen: id => editor.edited({ cleanup: id })
            }
            // The built-in rules the profile switched away from its level.
            Reveal {
                open: !editor.runMode && editor.profile.changedRules > 0
                LinkedText {
                    objectName: "cleanupChanged"
                    width: parent.width
                    plainText: Library.levelChanged(editor.level.label, editor.profile.changedRules || 0)
                    link: Library.rulesText.reset
                    variant: "small"
                    tone: "secondary"
                    onActivated: editor.rulesReset()
                }
            }
            InheritSelect {
                visible: editor.runMode
                inheritedText: Words.sameAs(editor.inherited.profileName || "", Library.levelLabel(editor.inherited.cleanup))
                choices: editor.levels
                currentId: editor.profile.cleanup || ""
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
                    text: Library.cleanupSamples[editor.level.id] || ""
                    tone: editor.level.id === "none" ? "secondary" : "primary"
                }
            }
        }
    }

    FormField {
        id: toneField
        label: Schema.row("profileTone").label
        SearchLanding { rowIds: editor.landsOn(["profileTone"]); target: toneField }
        help: editor.skipsRefinement ? Library.profilesText.toneSkipped : editor.tone.help
        // One tone from a list, so a menu. Skipped refinement leaves it
        // editable for later; the help says so.
        InheritSelect {
            width: Math.min(implicitWidth, parent.width)
            inheritedText: editor.runMode ? Words.sameAs(editor.inherited.profileName || "", Library.toneLabel(editor.inherited.tone)) : ""
            choices: editor.tones
            currentId: editor.runMode ? editor.profile.tone || "" : editor.tone.id
            onChosen: id => editor.edited({ tone: id })
        }
    }

    FormField {
        id: languageField
        visible: !editor.runMode
        width: parent.width * 0.4
        label: Schema.row("profileLanguage").label
        help: Library.profilesText.languageHelp
        SearchLanding { rowIds: editor.landsOn(["profileLanguage"]); target: languageField }
        SuggestField {
            objectName: "profileLanguage"
            suggestionsText: Words.suggestions
            width: parent.width
            iconName: "languages"
            text: editor.profile.language || ""
            placeholderText: Schema.row("profileLanguage").placeholder
            // Empty means the same as spoken, so at rest that reads as the
            // value, as a Select's default does; editing, a hint.
            placeholderTextColor: activeFocus ? Theme.textTertiary : Theme.text
            suggestions: Library.outputLanguages.map(l => ({ id: l, label: l }))
            onCommitted: v => editor.edited({ language: v.trim() })
        }
    }

    // The rules, one list per group, each with its switch for this profile.
    // Cleanup None skips them all; a custom system prompt the built-in ones.
    Column {
        id: rulesField
        visible: !editor.runMode
        width: parent.width
        spacing: Theme.space4
        SearchLanding { rowIds: editor.landsOn(["profileRules"]); target: rulesField }
        FormField {
            label: Schema.row("profileRules").label
            help: editor.skipsRefinement ? Library.rulesText.skipped
                : editor.customPrompt ? Library.rulesText.customPrompt : Schema.row("profileRules").help
        }
        Repeater {
            model: Library.ruleGroups
            Column {
                id: group
                required property var modelData
                readonly property bool yours: modelData.id === "yours"
                // { id, title, detail }: a built-in rule worded by its id, or
                // one of yours with its instruction.
                readonly property var rules: yours
                    ? Library.rules.map(r => ({ id: r.id, title: r.name, detail: r.instruction }))
                    : Library.builtInRules.filter(r => r.group === modelData.id)
                        .map(r => ({ id: r.id, title: Library.ruleWords[r.id].label, detail: Library.ruleWords[r.id].help }))
                width: parent.width
                Item {
                    width: parent.width
                    height: Math.max(groupTitle.implicitHeight, addRule.height)
                    Txt {
                        id: groupTitle
                        anchors.verticalCenter: parent.verticalCenter
                        text: group.modelData.label
                        variant: "label"
                        tone: "secondary"
                    }
                    Button {
                        id: addRule
                        anchors.right: parent.right
                        visible: group.yours
                        text: Library.rulesText.addRule
                        iconName: "plus"
                        size: "small"
                        onClicked: editor.newRuleRequested()
                    }
                }
                // Rows bring their own inset, so the list reaches out by as
                // much to line up.
                RowList {
                    id: ruleRows
                    x: -Theme.rowInset
                    width: parent.width + Theme.rowInset * 2
                    values: group.rules
                    keyOf: r => r.id
                    delegate: ListRow {
                        id: ruleRow
                        required property string key
                        required property int index
                        readonly property var rule: ruleRows.entry(key)
                        width: ListView.view.width
                        first: index === 0
                        title: rule.title
                        detail: rule.detail
                        trailing: Switch {
                            objectName: "ruleSwitch_" + ruleRow.key
                            checked: !!(editor.profile.rules || {})[ruleRow.key]
                            onToggled: editor.ruleSwitched(ruleRow.key, checked)
                        }
                    }
                }
                Reveal {
                    x: -Theme.rowInset
                    width: parent.width + Theme.rowInset * 2
                    open: group.yours && group.rules.length === 0
                    EmptyState { compact: true; title: Library.rulesText.noRules; text: Library.rulesText.rulesEmpty }
                }
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
        readonly property int changes: [editor.profile.speech, editor.profile.refinement].filter(s => !!s).length
        title: editor.servicesTitle
        help: editor.pipeline
        // What it inherits while it changes nothing, as an InheritSelect's
        // first option says it.
        trailingText: changes ? Words.count(changes, Library.profilesText.change, Library.profilesText.changes)
                    : editor.runMode ? Library.profilesText.sameAs(editor.inherited.profileName || "")
                    : Library.profilesText.sameAsOrders
        open: changes > 0
        SearchLanding {
            rowIds: editor.landsOn(["profileServices"])
            target: services
            onLanded: { services.open = true; services.highlight() }
        }

        SettingRow {
            title: Library.profilesText.speech
            help: editor.speechSkipped ? Library.skippedSpeechHelp(editor.speechSkipped, Models.service(editor.profile.speech).label,
                                                                    editor.spokenLanguageName)
                : editor.profile.speech ? Library.profilesText.ownSpeechHelp : Library.profilesText.defaultSpeechHelp
            InheritSelect {
                inheritedText: Words.sameAs(editor.speechPlace, Models.service(editor.inheritedSpeech).label)
                choices: Models.speechServices.map(s => ({ id: s.id, label: s.label }))
                currentId: editor.profile.speech || ""
                onChosen: id => editor.edited({ speech: id, speechModel: "" })
            }
        }
        // A model to choose, where the service runs one, as its order row
        // chooses it.
        SettingRow {
            title: Library.profilesText.speechModel
            shown: !!editor.modelSpeech && ModelText.choosesModel("speech", editor.modelSpeech)
            ServiceModelField {
                role: "speech"
                service: editor.modelSpeech
                value: editor.profile.speechModel || ""
                inheritedText: editor.inheritedSpeechModel
                onChosen: model => editor.edited({ speechModel: model })
            }
        }
        SettingRow {
            title: Library.profilesText.refinement
            help: editor.refinementOff ? Schema.row("refinementEnabled").offNote
                : editor.skipsRefinement ? Library.profilesText.refinementSkipped
                : editor.profile.refinement ? Library.ownRefinementHelp(ModelText.provider(editor.profile.refinement).label)
                : Library.profilesText.defaultRefinementHelp
            InheritSelect {
                enabled: !editor.skipsRefinement && !editor.refinementOff
                inheritedText: Words.sameAs(editor.refinementPlace, ModelText.provider(editor.inheritedRefiner).label)
                // A run may refine with nothing.
                choices: Models.refinementProviders.map(p => ({ id: p.id, label: p.label }))
                    .concat(editor.runMode ? [{ id: "none", label: ModelText.provider("none").label }] : [])
                currentId: editor.profile.refinement || ""
                onChosen: id => editor.edited({ refinement: id, refinementModel: "" })
            }
        }
        SettingRow {
            title: Library.profilesText.refinementModel
            shown: !!editor.modelRefiner && editor.modelRefiner !== "none"
            enabled: !editor.skipsRefinement && !editor.refinementOff
            ServiceModelField {
                role: "refinement"
                service: editor.modelRefiner
                value: editor.profile.refinementModel || ""
                inheritedText: editor.inheritedRefinementModel
                onChosen: model => editor.edited({ refinementModel: model })
            }
        }
    }
}
