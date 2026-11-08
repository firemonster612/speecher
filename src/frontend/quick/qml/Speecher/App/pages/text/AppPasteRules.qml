pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The exceptions to the delivery steps, on the Pasting page: only the rules
// that differ from them, app types first, then single apps, each with what
// it does on this computer (result). An app type's rule is its
// categoryPasteRule_<type> setting, where inherit is none; the rules for
// single apps are the applicationPasteRules setting, [{ app, method,
// enabled }], saved whole on every change. Add exception asks for the app
// type or app, then the rule, and shows what it does before saving. An app
// is named as this platform's targets report it (Schema.pasting.appId), or
// taken from the last dictation.
Section {
    id: section
    objectName: "appPasteRules"

    readonly property var rules: Settings.values.applicationPasteRules || []
    // The app types with a rule of their own.
    readonly property var types: Schema.appCategories.filter(c => (Settings.values["categoryPasteRule_" + c.id] || "inherit") !== "inherit")
    readonly property var exceptions: types.map(c => ({ id: "type:" + c.id, type: c, method: Settings.values["categoryPasteRule_" + c.id] }))
        .concat(rules.map((r, i) => ({ id: "app:" + r.app, app: r.app, method: r.method, enabled: r.enabled, index: i })))
    // The last app dictated into, while insights recorded it: { app, appId }.
    readonly property var lastApp: Insights.latest && Insights.latest.appId ? Insights.latest : null
    readonly property string singleApp: "app"
    readonly property var t: ({
        add: "Add exception",
        applies: "Applies to",
        singleApp: "A single app…",
        rule: "Rule",
        appId: Schema.pasting.appId.label,
        appIdHelp: Schema.pasting.appId.help,
        appIdPlaceholder: Schema.pasting.appId.example,
        lastApp: app => "Use " + app,
        result: "Result: ",
        remove: "Remove exception",
        none: "No exceptions",
        noneHelp: "Every app follows the steps above."
    })

    function save(next) { Settings.set("applicationPasteRules", next) }
    function update(index, change) {
        const next = rules.slice()
        next[index] = Object.assign({}, next[index], change)
        save(next)
    }
    // Undo puts the exception back, an app's where it was, keeping edits
    // made since. The toast outlives this page, so Undo goes to Settings,
    // not to the page.
    function remove(index) {
        const gone = rules[index]
        save(rules.filter((r, i) => i !== index))
        Toaster.show(Words.deleted(appName(gone.app)), { icon: "trash-2", actionText: Words.undo, action: () => {
            const back = (Settings.values.applicationPasteRules || []).slice()
            back.splice(Math.min(index, back.length), 0, gone)
            Settings.set("applicationPasteRules", back)
        } })
    }
    function removeType(type) {
        const id = "categoryPasteRule_" + type.id
        const method = Settings.values[id]
        Settings.set(id, "inherit")
        Toaster.show(Words.deleted(type.label), { icon: "trash-2", actionText: Words.undo, action: () => Settings.set(id, method) })
    }
    // The last part of a desktop ID, which is how most apps are known.
    function appName(app) { return app.split(".").pop() }
    // What a rule with method does here, for an exception's row and the
    // add form alike: the steps it leaves that this computer can run
    // (System.rulePlan), or that it is off, for an app's exception turned
    // off. AI coding apps running in a terminal follow a Terminals
    // exception instead, as core ranks them (resolvePasteRule).
    function result(method, typeId, off) {
        const terminalWins = typeId === "ai_coding" && (Settings.values.categoryPasteRule_terminal || "inherit") !== "inherit"
        return Schema.pasting.ruleResult(System.rulePlan(method), method, off, terminalWins)
    }

    title: Schema.row("pasteExceptions").label
    caption: Schema.pasting.precedence
    actions: [ Button { text: section.t.add; iconName: "plus"; size: "small"; onClicked: form.startAdd() } ]
    SearchLanding { rowIds: ["pasteExceptions"]; target: section }
    SearchLanding { rowIds: ["addPasteException"]; target: section; onLanded: form.startAdd() }

    // Insert's row above turns accessibility on, so this only says why.
    AccessibilityNotice {
        open: section.exceptions.length > 0 && (_check.reason === "off" || _check.reason === "unsupported")
        offersFix: false
        text: Schema.pasting.exceptionsNotice(System.identifiesAppsWithoutAccessibility)
    }

    InlineForm {
        id: form
        objectName: "ruleForm"
        // What the exception is for: an app type's id, or singleApp; and
        // its rule. Both start unchosen.
        property string target: ""
        property string method: ""
        // Why this form's last exception was refused; a new form starts without it.
        property string refusal: ""
        readonly property bool forApp: target === section.singleApp
        readonly property var targets: Schema.appCategories.filter(c => !section.types.includes(c))
            .map(c => ({ id: c.id, label: c.label, icon: c.icon, help: c.examples }))
            .concat([{ id: section.singleApp, label: section.t.singleApp, icon: "app-window", help: Schema.pasting.appId.help }])
        // A rule that changes something: never the no-change option for an
        // app type, which is no exception.
        readonly property var methods: Schema.pasting.ruleOptions(forApp).filter(o => o.id !== "inherit")
        function startAdd() { appId.text = ""; target = ""; method = ""; refusal = ""; open = true }
        title: section.t.add
        saveText: section.t.add
        cancelText: Words.cancel
        canSave: target !== "" && method !== "" && (!forApp || appId.text.trim().length > 0)
        onCancelled: open = false
        onSaved: {
            if (!forApp) {
                Settings.set("categoryPasteRule_" + target, method)
                open = false
                return
            }
            section.save(section.rules.concat([{ app: appId.text.trim(), method: method, enabled: true }]))
            // A refused rule, such as a second one for the same app, keeps the form open.
            refusal = Settings.errors.applicationPasteRules || ""
            open = refusal.length > 0
        }
        Row {
            width: parent.width
            spacing: Theme.space4
            FormField {
                width: (parent.width - parent.spacing) / 2
                label: section.t.applies
                Select {
                    width: parent.width
                    placeholder: Words.choose
                    options: form.targets
                    currentId: form.target
                    onChosen: id => { form.target = id; if (!form.methods.some(o => o.id === form.method)) form.method = "" }
                }
            }
            FormField {
                width: (parent.width - parent.spacing) / 2
                label: section.t.rule
                help: form.method ? section.t.result + section.result(form.method, form.forApp ? "" : form.target, false) : ""
                Select {
                    width: parent.width
                    placeholder: Words.choose
                    enabled: form.target !== ""
                    options: form.methods
                    currentId: form.method
                    onChosen: id => form.method = id
                }
            }
        }
        Reveal {
            open: form.forApp
            width: parent.width
            FormField {
                width: parent.width
                label: section.t.appId
                help: form.refusal || section.t.appIdHelp
                Row {
                    width: parent.width
                    spacing: Theme.space2
                    TextField {
                        id: appId
                        width: parent.width - (useLast.visible ? useLast.width + parent.spacing : 0)
                        iconName: "app-window"
                        placeholderText: section.t.appIdPlaceholder
                    }
                    // The app just dictated into, by the ID its target had.
                    Button {
                        id: useLast
                        visible: !!section.lastApp
                        text: section.lastApp ? section.t.lastApp(section.lastApp.app || section.lastApp.appId) : ""
                        onClicked: appId.text = section.lastApp.appId
                    }
                }
            }
        }
    }

    RowList {
        id: list
        values: section.exceptions
        delegate: ListRow {
            id: exceptionRow
            required property string key
            required property int index
            readonly property var exception: list.entry(key)
            readonly property bool forApp: !exception.type
            readonly property real dim: forApp && !exception.enabled ? 0.5 : 1
            objectName: "exception_" + key
            width: ListView.view.width
            first: index === 0 && list.first
            height: Math.max(Theme.rowHeight + Theme.space2, words.implicitHeight + 2 * Theme.space3)
            // A search for this exception lands here and marks it a moment.
            SearchLanding { rowIds: [exceptionRow.objectName]; target: exceptionRow; onLanded: landed.restart() }
            selected: landed.running
            Timer { id: landed; interval: Theme.ms(1300) }

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: exceptionRow.forApp ? section.appName(exceptionRow.exception.app) : ""
                iconName: exceptionRow.forApp ? "" : exceptionRow.exception.type.icon
                size: 30
                opacity: exceptionRow.dim
                Behavior on opacity { Ease {} }
            }
            // The name, an app's ID under it, and on a line of its own,
            // which wraps, what the exception does.
            Column {
                id: words
                x: 42
                width: parent.width - x - choices.width - Theme.space4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                opacity: exceptionRow.dim
                Behavior on opacity { Ease {} }
                Txt {
                    width: parent.width
                    elide: Text.ElideRight
                    text: exceptionRow.forApp ? section.appName(exceptionRow.exception.app) : exceptionRow.exception.type.label
                    variant: "label"
                }
                Txt {
                    visible: exceptionRow.forApp
                    width: parent.width
                    elide: Text.ElideMiddle
                    text: exceptionRow.forApp ? exceptionRow.exception.app : ""
                    variant: "small"
                    tone: "tertiary"
                }
                Txt {
                    objectName: "exceptionResult"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: exceptionRow.forApp ? section.result(exceptionRow.exception.method, "", !exceptionRow.exception.enabled)
                                              : section.result(exceptionRow.exception.method, exceptionRow.exception.type.id, false)
                    variant: "small"
                    tone: "secondary"
                }
            }
            Row {
                id: choices
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space3
                Select {
                    implicitWidth: 220
                    options: Schema.pasting.ruleOptions(exceptionRow.forApp)
                    currentId: exceptionRow.exception.method
                    enabled: !exceptionRow.forApp || exceptionRow.exception.enabled
                    anchors.verticalCenter: parent.verticalCenter
                    // An app type set to the steps above has no exception left.
                    onChosen: id => exceptionRow.forApp ? section.update(exceptionRow.exception.index, { method: id })
                                                        : Settings.set("categoryPasteRule_" + exceptionRow.exception.type.id, id)
                }
                Switch {
                    visible: exceptionRow.forApp
                    checked: !!exceptionRow.exception.enabled
                    anchors.verticalCenter: parent.verticalCenter
                    onToggled: section.update(exceptionRow.exception.index, { enabled: checked })
                }
            }
            actions: [
                IconButton {
                    iconName: "trash-2"
                    size: 28
                    tooltip: section.t.remove
                    onClicked: exceptionRow.forApp ? section.remove(exceptionRow.exception.index) : section.removeType(exceptionRow.exception.type)
                }
            ]
        }
    }

    Reveal {
        open: section.exceptions.length === 0 && !form.open
        EmptyState {
            compact: true
            title: section.t.none
            text: section.t.noneHelp
        }
    }
}
