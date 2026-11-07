pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Rules for single apps: the add form, then each app with its rule and a
// switch, on the Pasting page. The list is the applicationPasteRules
// setting, [{ app, method, enabled }], saved whole on every change. An app
// is named as this platform's targets report it (Schema.pasting.appId).
// --pasting-form=rule opens the form.
Section {
    id: section
    objectName: "appPasteRules"

    // A rule's Same as above is Standard paste, which keeps it a rule of
    // its own over the app's type.
    readonly property var methods: Schema.pasting.ruleOptions("standard_paste")
    readonly property var rules: Settings.values.applicationPasteRules || []
    readonly property var t: ({
        addRule: "Add rule",
        newRule: "New app rule",
        ruleApp: Schema.pasting.appId.label,
        ruleAppHelp: Schema.pasting.appId.help,
        ruleAppPlaceholder: Schema.pasting.appId.example,
        ruleMethod: "Steps",
        deleteRule: "Delete rule",
        noRules: "No rules for single apps",
        noRulesHelp: "Add one to deliver differently into one app."
    })

    function save(next) { Settings.set("applicationPasteRules", next) }
    function update(index, change) {
        const next = rules.slice()
        next[index] = Object.assign({}, next[index], change)
        save(next)
    }
    // Undo puts the rule back where it was, keeping edits made since. The
    // toast outlives this page, so Undo goes to Settings, not to the page.
    function remove(index) {
        const gone = rules[index]
        save(rules.filter((r, i) => i !== index))
        Toaster.show(Words.deleted(appName(gone.app)), { icon: "trash-2", actionText: Words.undo, action: () => {
            const back = (Settings.values.applicationPasteRules || []).slice()
            back.splice(Math.min(index, back.length), 0, gone)
            Settings.set("applicationPasteRules", back)
        } })
    }
    // The last part of a desktop ID, which is how most apps are known.
    function appName(app) { return app.split(".").pop() }

    title: Schema.row("applicationPasteRules").label
    caption: Schema.row("applicationPasteRules").help
    actions: [ Button { text: section.t.addRule; iconName: "plus"; size: "small"; onClicked: ruleForm.startAdd() } ]
    SearchLanding { rowIds: ["applicationPasteRules"]; target: section }

    InlineForm {
        cancelText: Words.cancel
        id: ruleForm
        objectName: "ruleForm"
        // The first override, as Same as above is no change.
        property string method: section.methods[1].id
        // Why this form's last rule was refused; a new form starts without it.
        property string refusal: ""
        function startAdd() { ruleApp.text = ""; method = section.methods[1].id; refusal = ""; open = true }
        title: section.t.newRule
        saveText: section.t.addRule
        canSave: ruleApp.text.trim().length > 0
        onCancelled: open = false
        onSaved: {
            section.save(section.rules.concat([{ app: ruleApp.text.trim(), method: method, enabled: true }]))
            // A refused rule, such as a second one for the same app, keeps the form open.
            refusal = Settings.errors.applicationPasteRules || ""
            open = refusal.length > 0
        }
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField {
                width: (parent.width - parent.spacing) * 0.55
                label: section.t.ruleApp
                help: ruleForm.refusal || section.t.ruleAppHelp
                TextField {
                    id: ruleApp
                    width: parent.width
                    iconName: "app-window"
                    placeholderText: section.t.ruleAppPlaceholder
                }
            }
            FormField {
                width: (parent.width - parent.spacing) * 0.45
                label: section.t.ruleMethod
                Select { width: parent.width; options: section.methods; currentId: ruleForm.method; onChosen: id => ruleForm.method = id }
            }
        }
    }

    RowList {
        id: appRules
        values: section.rules
        keyOf: r => r.app
        delegate: ListRow {
            id: ruleRow
            required property string key
            required property int index
            readonly property var rule: appRules.entry(key)
            readonly property real dim: rule.enabled ? 1 : 0.5
            width: ListView.view.width
            first: index === 0 && appRules.first
            height: Theme.rowHeight + Theme.space2

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: section.appName(ruleRow.rule.app)
                size: 30
                opacity: ruleRow.dim
                Behavior on opacity { Ease {} }
            }
            Column {
                x: 42
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                opacity: ruleRow.dim
                Behavior on opacity { Ease {} }
                Txt { text: section.appName(ruleRow.rule.app); variant: "label" }
                // The step the rule relies on that's off, else the app's ID.
                Txt {
                    readonly property string needs: System.stepRuleNeeds(ruleRow.rule.method)
                    text: needs ? Schema.pasting.needsStep(needs) : ruleRow.rule.app
                    variant: needs ? "small" : "code"
                    tone: "secondary"
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3
                Select {
                    implicitWidth: 190
                    options: section.methods
                    currentId: ruleRow.rule.method
                    enabled: ruleRow.rule.enabled
                    anchors.verticalCenter: parent.verticalCenter
                    onChosen: id => section.update(ruleRow.index, { method: id })
                }
                Switch {
                    checked: ruleRow.rule.enabled
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: section.update(ruleRow.index, { enabled: checked })
                }
            }
            actions: [
                IconButton {
                    iconName: "trash-2"
                    size: 28
                    tooltip: section.t.deleteRule
                    onClicked: section.remove(ruleRow.index)
                }
            ]
        }
    }

    Reveal {
        open: section.rules.length === 0 && !ruleForm.open
        EmptyState {
            compact: true
            title: section.t.noRules
            text: section.t.noRulesHelp
        }
    }
}
