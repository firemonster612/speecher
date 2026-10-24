pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Launch
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

Page {
    id: page
    title: Schema.page("profiles").title
    subtitle: Schema.page("profiles").subtitle
    maxWidth: 920

    readonly property var profiles: Library.profiles
    // Preview state: --profile=<id> opens that profile, --profiles-form=tone|level opens a form.
    property string selectedId: Launch.arg("profile", "work")
    readonly property int selectedIndex: Math.max(0, profiles.findIndex(p => p.id === selectedId))
    readonly property var selected: profiles[selectedIndex]

    function addProfile() {
        const id = Library.newId("custom_profile")
        Library.add("profiles", { id: id, name: "New profile", icon: "pen-line", builtIn: false, cleanup: "balanced", tone: "none",
                                  language: "", instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" })
        selectedId = id
    }

    // Profiles on the left, the chosen one's settings on the right, on one
    // card.
    Section {
        id: profilesSection
        SearchLanding { rowIds: ["writingProfileBehavior"]; target: profilesSection }
        Item {
            width: parent.width
            height: Math.max(list.implicitHeight + Theme.space2, editorPane.implicitHeight + Theme.space5 * 2)

            Column {
                id: list
                x: Theme.space1
                y: Theme.space1
                width: 244
                spacing: Theme.space1

                ChoiceRows {
                    indicator: "check"
                    options: page.profiles.map(p => ({ id: p.id, label: p.name, icon: p.icon, help: Library.profileSummary(p) }))
                    currentId: page.selected.id
                    onActivated: id => page.selectedId = id
                }
                Button {
                    x: Theme.space2
                    text: Library.profilesText.addProfile
                    iconName: "plus"
                    variant: "ghost"
                    onClicked: page.addProfile()
                }
            }

            Rectangle {
                id: split
                x: list.x + list.width + Theme.space1
                width: 1
                height: parent.height
                color: Theme.rule
            }

            ProfileEditor {
                id: editorPane
                x: split.x + Theme.space6
                y: Theme.space5
                width: parent.width - x - Theme.space6
                profile: page.selected
                isFallback: page.selected.id === Settings.values.defaultWritingProfile
                apps: Library.profileApps(page.selected.id)
                onEdited: patch => Library.update("profiles", page.selectedIndex, patch)
                onDeleteRequested: {
                    Library.removeProfile(page.selected.id)
                    page.selectedId = "work"
                }
                onNewToneRequested: { toneForm.startAdd(); page.reveal(toneSection) }
            }
        }
    }

    Section {
        title: Schema.row("defaultWritingProfile").section
        SchemaRow {
            rowId: "defaultWritingProfile"
            Select {
                options: page.profiles.map(p => ({ id: p.id, label: p.name, icon: p.icon }))
                currentId: Settings.values.defaultWritingProfile || ""
                onChosen: id => Settings.set("defaultWritingProfile", id)
            }
        }
        SettingRow {
            title: Library.profilesText.appMatches
            help: Library.profilesText.appMatchesHelp
            clickable: true
            trailingText: Library.recognitionRulesText
            onClicked: Navigation.goToRow("appRecognitionRules")
        }
        SchemaRow {
            rowId: "additionalInstructions"
            stacked: true
            TextArea {
                width: parent.width
                text: Library.additionalInstructions
                placeholderText: Schema.row("additionalInstructions").placeholder
                onEditingFinished: Library.additionalInstructions = text
            }
        }
    }

    // Your own tones and cleanup levels, side by side.
    Row {
        width: parent.width
        spacing: Theme.space4

        Section {
            id: toneSection
            width: (parent.width - parent.spacing) / 2
            minimumHeaderHeight: levelSection.naturalHeaderHeight
            title: Schema.row("customTones").label
            caption: Schema.row("customTones").help
            actions: [ Button { text: Library.profilesText.addTone; iconName: "plus"; size: "small"; onClicked: toneForm.startAdd() } ]
            SearchLanding { rowIds: ["customTones"]; target: toneSection }

            EditableList {
                editText: Words.edit
                removeText: Words.delete_
                items: Library.customTones.map(t => ({ id: t.id, title: t.name, detail: t.instruction }))
                formOpen: toneForm.open
                emptyText: Library.profilesText.tonesEmpty
                onEditClicked: i => toneForm.startEdit(i)
                onRemoveClicked: i => Library.removeTone(Library.customTones[i].id)

                InlineForm {
                    cancelText: Words.cancel
                    id: toneForm
                    property int editing: -1
                    function startAdd() { editing = -1; toneName.text = ""; toneText.text = ""; open = true }
                    function startEdit(i) {
                        editing = i
                        toneName.text = Library.customTones[i].name
                        toneText.text = Library.customTones[i].instruction
                        open = true
                    }
                    title: editing < 0 ? Library.profilesText.newTone : Library.profilesText.editTone
                    saveText: editing < 0 ? Library.profilesText.addTone : Words.save
                    canSave: toneName.text.trim().length > 0 && toneText.text.trim().length > 0
                    open: Launch.arg("profiles-form", "") === "tone"
                    onCancelled: open = false
                    onSaved: {
                        const tone = { name: toneName.text.trim(), instruction: toneText.text }
                        if (editing < 0) Library.add("customTones", Object.assign({ id: Library.newId("custom_tone") }, tone))
                        else Library.update("customTones", editing, tone)
                        open = false
                    }
                    FormField { label: Library.profilesText.name; TextField { id: toneName; width: parent.width; placeholderText: Library.profilesText.toneNamePlaceholder } }
                    FormField {
                        label: Library.profilesText.toneInstruction
                        help: Library.profilesText.toneInstructionHelp
                        TextArea { id: toneText; width: parent.width; minHeight: 60; placeholderText: Library.profilesText.toneInstructionPlaceholder }
                    }
                }
            }
        }

        Section {
            id: levelSection
            width: (parent.width - parent.spacing) / 2
            minimumHeaderHeight: toneSection.naturalHeaderHeight
            title: Schema.row("customCleanupLevels").label
            caption: Schema.row("customCleanupLevels").help
            actions: [ Button { text: Library.profilesText.addLevel; iconName: "plus"; size: "small"; onClicked: levelForm.startAdd() } ]
            SearchLanding { rowIds: ["customCleanupLevels"]; target: levelSection }

            EditableList {
                editText: Words.edit
                removeText: Words.delete_
                items: Library.customCleanupLevels.map(l => ({ id: l.id, title: l.name, detail: Library.levelDetail(l) }))
                formOpen: levelForm.open
                emptyText: Library.profilesText.levelsEmpty
                onEditClicked: i => levelForm.startEdit(i)
                onRemoveClicked: i => Library.removeLevel(Library.customCleanupLevels[i].id)

                InlineForm {
                    cancelText: Words.cancel
                    id: levelForm
                    property int editing: -1
                    property string base: "balanced"
                    function startAdd() { editing = -1; levelName.text = ""; levelText.text = ""; base = "balanced"; open = true }
                    function startEdit(i) {
                        const l = Library.customCleanupLevels[i]
                        editing = i
                        levelName.text = l.name
                        levelText.text = l.instructions
                        base = l.base
                        open = true
                    }
                    title: editing < 0 ? Library.profilesText.newLevel : Library.profilesText.editLevel
                    saveText: editing < 0 ? Library.profilesText.addLevel : Words.save
                    canSave: levelName.text.trim().length > 0 && (base !== "custom_only" || levelText.text.trim().length > 0)
                    open: Launch.arg("profiles-form", "") === "level"
                    onCancelled: open = false
                    onSaved: {
                        const level = { name: levelName.text.trim(), base: base, instructions: levelText.text }
                        if (editing < 0) Library.add("customCleanupLevels", Object.assign({ id: Library.newId("custom_level") }, level))
                        else Library.update("customCleanupLevels", editing, level)
                        open = false
                    }
                    FormField { label: Library.profilesText.name; TextField { id: levelName; width: parent.width; placeholderText: Library.profilesText.levelNamePlaceholder } }
                    FormField {
                        label: Library.profilesText.basedOn
                        SegmentedControl {
                            width: parent.width
                            stretch: true
                            options: Library.cleanupLevels.slice(1).map(l => ({ id: l.id, label: l.label })).concat([{ id: "custom_only", label: Library.profilesText.customOnly }])
                            currentId: levelForm.base
                            onActivated: id => levelForm.base = id
                        }
                    }
                    FormField {
                        label: Library.profilesText.levelInstructions
                        TextArea { id: levelText; width: parent.width; minHeight: 60; placeholderText: Library.profilesText.levelInstructionsPlaceholder }
                    }
                }
            }
        }
    }
}
