pragma ComponentBehavior: Bound
import QtQuick
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

    readonly property var profiles: Library.profiles
    property string selectedId: "work"
    readonly property int selectedIndex: Math.max(0, profiles.findIndex(p => p.id === selectedId))
    readonly property var selected: profiles[selectedIndex]

    function addProfile() {
        const id = Library.newId("custom_profile")
        if (!Library.add("profiles", { id: id, name: Library.newProfileName(), cleanup: "balanced", tone: "none", language: "",
                                       instructions: "", speech: "", speechModel: "", refinement: "", refinementModel: "" }))
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

                // Which profile the editor shows: a selected row, not a check,
                // since choosing one here changes nothing.
                RowList {
                    id: profileRows
                    width: parent.width
                    values: page.profiles
                    keyOf: p => p.id
                    delegate: ListRow {
                        required property string key
                        required property int index
                        readonly property var profile: profileRows.entry(key)
                        width: ListView.view.width
                        first: index === 0
                        title: profile.name
                        detail: Library.profileSummary(profile)
                        iconName: Library.profileIcon(key)
                        clickable: true
                        selected: key === page.selected.id
                        Accessible.selected: selected
                        onClicked: page.selectedId = key
                    }
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
                onEdited: patch => refusal = Library.refusalText("profiles", Library.update("profiles", page.selected.id, patch))
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
                options: page.profiles.map(p => ({ id: p.id, label: p.name, icon: Library.profileIcon(p.id) }))
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
                text: Settings.values.additionalInstructions || ""
                placeholderText: Schema.row("additionalInstructions").placeholder
                onEditingFinished: Settings.set("additionalInstructions", text)
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
                onRemoveClicked: i => {
                    const name = Library.customTones[i].name
                    const undo = Library.removeTone(Library.customTones[i].id)
                    if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
                }

                InlineForm {
                    cancelText: Words.cancel
                    id: toneForm
                    // The tone being edited, by id, or empty while adding.
                    property string editing
                    function startAdd() { editing = ""; toneName.text = ""; toneText.text = ""; error = ""; open = true }
                    function startEdit(i) {
                        const t = Library.customTones[i]
                        editing = t.id
                        toneName.text = t.name
                        toneText.text = t.instruction
                        error = ""
                        open = true
                    }
                    title: editing ? Library.profilesText.editTone : Library.profilesText.newTone
                    saveText: editing ? Words.save : Library.profilesText.addTone
                    canSave: toneName.text.trim().length > 0 && toneText.text.trim().length > 0
                    onCancelled: open = false
                    onSaved: {
                        const tone = { name: toneName.text.trim(), instruction: toneText.text }
                        // A profile's tone choices list the built-in ones by these names too.
                        const builtIns = Library.tones.map(t => t.label)
                        const refused = editing ? Library.update("customTones", editing, tone, builtIns)
                                                : Library.add("customTones", Object.assign({ id: Library.newId("custom_tone") }, tone), builtIns)
                        error = Library.refusalText("customTones", refused)
                        if (!refused) open = false
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
                onRemoveClicked: i => {
                    const name = Library.customCleanupLevels[i].name
                    const undo = Library.removeLevel(Library.customCleanupLevels[i].id)
                    if (undo >= 0) Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
                }

                InlineForm {
                    cancelText: Words.cancel
                    id: levelForm
                    // The level being edited, by id, or empty while adding.
                    property string editing
                    property string base: "balanced"
                    function startAdd() { editing = ""; levelName.text = ""; levelText.text = ""; base = "balanced"; error = ""; open = true }
                    function startEdit(i) {
                        const l = Library.customCleanupLevels[i]
                        editing = l.id
                        levelName.text = l.name
                        levelText.text = l.instructions
                        base = l.base
                        error = ""
                        open = true
                    }
                    title: editing ? Library.profilesText.editLevel : Library.profilesText.newLevel
                    saveText: editing ? Words.save : Library.profilesText.addLevel
                    canSave: levelName.text.trim().length > 0 && (base !== "custom_only" || levelText.text.trim().length > 0)
                    onCancelled: open = false
                    onSaved: {
                        const level = { name: levelName.text.trim(), base: base, instructions: levelText.text }
                        const builtIns = Library.cleanupLevels.map(l => l.label)
                        const refused = editing ? Library.update("customCleanupLevels", editing, level, builtIns)
                                                : Library.add("customCleanupLevels", Object.assign({ id: Library.newId("custom_level") }, level), builtIns)
                        error = Library.refusalText("customCleanupLevels", refused)
                        if (!refused) open = false
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
