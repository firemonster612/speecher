pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Launch
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Rules for single apps: the add form, then each app with its paste method
// and a switch, on the Pasting page. The list is the applicationPasteRules
// setting, [{ app, method, enabled }], saved whole on every change.
// --pasting-form=rule opens the form.
Section {
    id: section

    readonly property var methods: Schema.row("globalPasteRule").options
    readonly property var rules: Settings.values.applicationPasteRules || []
    readonly property var t: ({
        addRule: "Add rule",
        newRule: "New paste rule",
        ruleApp: "Application ID",
        ruleAppHelp: "The app's desktop ID, such as org.kde.konsole.",
        ruleAppPlaceholder: "org.kde.konsole",
        ruleMethod: "Paste method",
        deleteRule: "Delete rule",
        noRules: "No rules for single apps",
        noRulesHelp: "Add one to paste differently into one application."
    })

    function save(next) { Settings.set("applicationPasteRules", next) }
    function update(index, change) {
        const next = rules.slice()
        next[index] = Object.assign({}, next[index], change)
        save(next)
    }
    // Undo puts the rule back where it was, keeping edits made since.
    function remove(index) {
        const gone = rules[index]
        save(rules.filter((r, i) => i !== index))
        Toaster.show(Words.deleted(appName(gone.app)), { icon: "trash-2", actionText: Words.undo, action: () => {
            const back = section.rules.slice()
            back.splice(Math.min(index, back.length), 0, gone)
            section.save(back)
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
        property string method: "standard_paste"
        // Why this form's last rule was refused; a new form starts without it.
        property string refusal: ""
        function startAdd() { ruleApp.text = ""; method = "standard_paste"; refusal = ""; open = true }
        title: section.t.newRule
        saveText: section.t.addRule
        canSave: ruleApp.text.trim().length > 0
        open: Launch.arg("pasting-form", "") === "rule"
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
                Txt { text: ruleRow.rule.app; variant: "code"; tone: "secondary" }
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
