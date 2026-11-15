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
    objectName: "profilesPage"
    title: Schema.page("profiles").title
    subtitle: Schema.page("profiles").subtitle

    readonly property var profiles: Library.profiles
    property string selectedId: "work"
    readonly property int selectedIndex: Math.max(0, profiles.findIndex(p => p.id === selectedId))
    readonly property var selected: profiles[selectedIndex]

    function addProfile() {
        const id = Library.newId("custom_profile")
        if (!Library.add("profiles", { id: id, name: Library.newProfileName(), cleanup: "balanced", tone: "none", language: "",
                                       speech: "", speechModel: "", refinement: "", refinementModel: "" }))
            selectedId = id
    }

    // A search for a profile shows it in the editor; one for your rule
    // opens it to edit.
    function landOn(rowId) {
        const at = rowId.indexOf(":")
        const id = rowId.slice(at + 1)
        if (rowId.slice(0, at) === "rule") ruleForm.startEdit(id)
        else selectedId = id
    }

    // Profiles on the left, the chosen one's settings on the right, on one
    // card.
    Section {
        id: profilesSection
        SearchLanding { rowIds: ["writingProfileBehavior"]; target: profilesSection }
        SearchLanding {
            rowIds: page.profiles.map(p => "profile:" + p.id)
            target: profilesSection
            onLanded: rowId => page.landOn(rowId)
        }
        // Without app detection, every dictation takes the profile for apps
        // Speecher doesn't recognize.
        AccessibilityNotice { shown: !System.identifiesAppsWithoutAccessibility; what: Schema.pasting.accessibilityUses.profiles }
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
                apps: Library.profileUsers(page.selected.id)
                onEdited: patch => refusal = Library.refusalText("profiles", Library.update("profiles", page.selected.id, patch))
                onDeleteRequested: {
                    Library.removeProfile(page.selected.id)
                    page.selectedId = "work"
                }
                onRuleSwitched: (rule, on) => refusal = Library.refusalText("rules", Library.setProfileRule(page.selected.id, rule, on))
                onRulesReset: refusal = Library.refusalText("profiles", Library.resetRules(page.selected.id))
                onNewRuleRequested: { ruleForm.startAdd(page.selected.id); page.reveal(rulesSection) }
            }
        }
    }

    // Your rules, each on in the profiles that turn it on.
    Section {
        id: rulesSection
        title: Schema.row("yourRules").label
        caption: Schema.row("yourRules").help
        actions: [ Button { text: Library.rulesText.addRule; iconName: "plus"; size: "small"; onClicked: ruleForm.startAdd("") } ]
        SearchLanding { rowIds: ["yourRules"]; target: rulesSection }
        SearchLanding {
            rowIds: Library.rules.map(r => "rule:" + r.id)
            target: rulesSection
            onLanded: rowId => page.landOn(rowId)
        }

        EditableList {
            editText: Words.edit
            removeText: Words.delete_
            items: Library.rules.map(r => ({ id: r.id, title: r.name, detail: r.instruction, trailing: Library.ruleWhere(r) }))
            formOpen: ruleForm.open
            emptyTitle: Library.rulesText.noRules
            emptyText: Library.rulesText.rulesEmpty
            onEditClicked: i => ruleForm.startEdit(Library.rules[i].id)
            onRemoveClicked: i => {
                // Copied before removing: Library.rules[i] reads the list as
                // it is when used, which the removal changes.
                const name = Library.rules[i].name
                const undo = Library.remove("rules", Library.rules[i].id)
                if (undo >= 0) Toaster.show(Words.deleted(name),{ icon: "trash-2", actionText: Words.undo, action: () => Library.undo(undo) })
            }

            InlineForm {
                id: ruleForm
                objectName: "ruleForm"
                cancelText: Words.cancel
                // The rule being edited, by id, or empty while adding.
                property string editing
                // The profile whose editor asked for the form, which a new
                // rule not on in every profile is switched on for; or empty.
                property string forProfile
                function startAdd(profile) {
                    editing = ""
                    forProfile = profile
                    ruleName.text = ""
                    ruleText.text = ""
                    everyProfile.checked = false
                    error = ""
                    open = true
                }
                function startEdit(id) {
                    const rule = Library.rules.find(r => r.id === id)
                    if (!rule) return
                    editing = id
                    forProfile = ""
                    ruleName.text = rule.name
                    ruleText.text = rule.instruction
                    everyProfile.checked = rule.everyProfile
                    error = ""
                    open = true
                }
                title: editing ? Library.rulesText.editRule : Library.rulesText.newRule
                saveText: editing ? Words.save : Library.rulesText.addRule
                note: Words.savesOnPress(saveText)
                canSave: ruleName.text.trim().length > 0
                onCancelled: open = false
                onSaved: {
                    const rule = { name: ruleName.text.trim(), instruction: ruleText.text, everyProfile: everyProfile.checked }
                    const id = editing || Library.newId("custom_rule")
                    const refused = editing ? Library.update("rules", id, rule) : Library.add("rules", Object.assign({ id: id }, rule))
                    error = Library.refusalText("rules", refused)
                    if (refused) return
                    if (!editing && forProfile && !rule.everyProfile) Library.setProfileRule(forProfile, id, true)
                    open = false
                }
                FormField {
                    label: Library.profilesText.name
                    TextField { id: ruleName; width: parent.width; placeholderText: Library.rulesText.namePlaceholder }
                }
                FormField {
                    label: Library.rulesText.instruction
                    help: Library.rulesText.instructionHelp
                    TextArea { id: ruleText; width: parent.width; minHeight: 60; placeholderText: Library.rulesText.instructionPlaceholder }
                }
                FormField {
                    label: Library.rulesText.onEverywhere
                    help: ruleForm.forProfile ? Library.rulesText.onlyIn(Library.profileName(ruleForm.forProfile))
                                              : Library.rulesText.onEverywhereHelp
                    Switch { id: everyProfile }
                }
            }
        }
    }
}
