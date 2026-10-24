pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Launch
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Rules for single apps: the add form, then each app with its paste method
// and a switch, on the Pasting page. --pasting-form=rule opens the form.
Section {
    id: section

    readonly property var methods: Schema.row("globalPasteRule").options

    title: Schema.row("applicationPasteRules").label
    caption: Schema.row("applicationPasteRules").help
    actions: [ Button { text: Library.pastingText.addRule; iconName: "plus"; size: "small"; onClicked: ruleForm.startAdd() } ]
    SearchLanding { rowIds: ["applicationPasteRules"]; target: section }

    InlineForm {
        cancelText: Words.cancel
        id: ruleForm
        objectName: "ruleForm"
        property string method: "standard"
        function startAdd() { ruleApp.text = ""; method = "standard"; open = true }
        title: Library.pastingText.newRule
        saveText: Library.pastingText.addRule
        canSave: ruleApp.text.trim().length > 0
        open: Launch.arg("pasting-form", "") === "rule"
        onCancelled: open = false
        onSaved: {
            const id = ruleApp.text.trim()
            Library.add("appPasteRules", { app: id, name: id.split(".").pop(), method: method, enabled: true })
            open = false
        }
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField {
                width: (parent.width - parent.spacing) * 0.55
                label: Library.pastingText.ruleApp
                help: Library.pastingText.ruleAppHelp
                SuggestField {
                    suggestionsText: Words.suggestions
                    id: ruleApp
                    width: parent.width
                    iconName: "app-window"
                    placeholderText: Library.pastingText.ruleAppPlaceholder
                    suggestions: Library.openApps.map(a => ({ id: a, label: a, badge: Library.pastingText.openNow }))
                }
            }
            FormField {
                width: (parent.width - parent.spacing) * 0.45
                label: Library.pastingText.ruleMethod
                Select { width: parent.width; options: section.methods; currentId: ruleForm.method; onChosen: id => ruleForm.method = id }
            }
        }
    }

    RowList {
        id: appRules
        values: Library.appPasteRules
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
                name: ruleRow.rule.name
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
                Txt { text: ruleRow.rule.name; variant: "label" }
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
                    onChosen: id => Library.update("appPasteRules", ruleRow.index, { method: id })
                }
                Switch {
                    checked: ruleRow.rule.enabled
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: Library.update("appPasteRules", ruleRow.index, { enabled: checked })
                }
            }
            actions: [
                IconButton { iconName: "trash-2"; size: 28; tooltip: Library.pastingText.deleteRule; onClicked: { const name = ruleRow.rule.name; const undo = Library.remove("appPasteRules", ruleRow.index); Toaster.show(Words.deleted(name), { icon: "trash-2", actionText: Words.undo, action: undo }) } }
            ]
        }
    }

    Reveal {
        open: Library.appPasteRules.length === 0 && !ruleForm.open
        EmptyState {
            iconName: "app-window"
            title: Library.pastingText.noRules
            text: Library.pastingText.noRulesHelp
        }
    }
}
