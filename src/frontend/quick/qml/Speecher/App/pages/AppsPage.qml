pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "apps"

// How Speecher writes and pastes in each app: the App Types, Other apps
// last, each with the person's Apps of that type under it, then any Apps
// matched only by window title, which follow no one type. Each opens in
// place to the same editor; a type's editor also lists the apps Speecher
// puts in it that have nothing of their own yet. Add app opens the picker,
// closing any editor; an app picked there opens its editor. Test shows what
// Speecher makes of the app you switch to.
Page {
    id: page
    title: Schema.page("apps").title
    subtitle: Schema.page("apps").subtitle

    readonly property var t: Schema.apps.t
    // The App or App Type whose editor is open, by its landing id
    // ("appentry:<id>" or "apptype:<id>"); one at a time.
    property string openId: ""
    // The editor, picker, Test and New type form open one at a time.
    onOpenIdChanged: if (openId) { picker.close(); tester.close(); newType.open = false }
    function toggle(id) { openId = openId === id ? "" : id }

    function startType() {
        openId = ""
        picker.close()
        tester.close()
        typeName.text = ""
        newType.error = ""
        newType.open = true
        typeName.forceActiveFocus()
    }

    // Each type's row, then its Apps' rows: { id (the landing id), isType,
    // value (of Apps.types or Apps.entries) }.
    readonly property var grouped: Apps.types.reduce((rows, type) => rows.concat(
        [{ id: "apptype:" + type.id, isType: true, value: type }],
        Apps.entries.filter(e => !e.titleOnly && e.effective.type === type.id)
            .map(e => ({ id: "appentry:" + e.id, isType: false, value: e }))), [])
    readonly property var titleOnly: Apps.entries.filter(e => e.titleOnly)
        .map(e => ({ id: "appentry:" + e.id, isType: false, value: e }))
    // The apps Speecher puts in type `id` that have nothing of their own:
    // installed ones, and those dictated into lately. Other apps lists only
    // the latter, not every app Speecher doesn't recognize.
    function detectedIn(id) {
        const other = id === "general"
        const recent = Apps.recent.filter(a => !a.entry && a.type === id && (other || !a.listedInstalled))
            .map(a => ({ name: a.name, icon: a.icon, ids: [a.appId] }))
        const installed = other ? [] : Apps.installed.filter(a => !a.entry && a.type === id)
            .map(a => ({ name: a.name, icon: a.icon, ids: a.ids }))
        return recent.concat(installed)
    }
    // Gives a listed app settings of its own and opens it, as picking it
    // with Add app does.
    function adopt(app) {
        const id = Apps.add(app).id
        if (id) Navigation.goToRow("apps", "appentry:" + id)
    }

    Component.onCompleted: Apps.refresh()

    Section {
        id: section
        title: page.t.types
        caption: page.t.typesHelp
        actions: [
            Button {
                objectName: "testApp"
                text: page.t.test
                iconName: "scan-search"
                size: "small"
                variant: "ghost"
                onClicked: tester.start()
            },
            Button { objectName: "newAppType"; text: page.t.newType; iconName: "plus"; size: "small"; variant: "ghost"; onClicked: page.startType() },
            Button { text: page.t.addApp; iconName: "plus"; size: "small"; onClicked: picker.start() }
        ]
        // The History row is in each editor; a search for it lands on the
        // list.
        SearchLanding { rowIds: ["apps", "appHistory", "appTypes"]; target: section }
        SearchLanding { rowIds: ["newAppType"]; target: section; onLanded: page.startType() }
        SearchLanding { rowIds: ["addApp"]; target: section; onLanded: picker.start() }
        SearchLanding { rowIds: ["otherAppsProfile"]; target: section; onLanded: page.openId = "apptype:general" }
        AccessibilityNotice { what: Schema.pasting.accessibilityUses[System.identifiesAppsWithoutAccessibility ? "inserting" : "apps"] }

        InlineForm {
            id: newType
            objectName: "newAppTypeForm"
            width: parent.width
            title: page.t.newType
            saveText: Words.save
            cancelText: Words.cancel
            note: Words.savesOnPress(saveText)
            canSave: typeName.text.trim().length > 0
            onCancelled: open = false
            onSaved: {
                const added = Apps.addType(typeName.text)
                error = Schema.apps.typeRefusal(added.refusal)
                if (added.id) {
                    open = false
                    page.openId = "apptype:" + added.id
                }
            }
            FormField {
                label: page.t.appName
                TextField { id: typeName; objectName: "newAppTypeName"; width: parent.width }
            }
        }
        AppTest {
            id: tester
            onOpenChanged: if (open) { page.openId = ""; picker.close(); newType.open = false }
        }
        AppPicker {
            id: picker
            onOpenChanged: if (open) { page.openId = ""; tester.close(); newType.open = false }
            // Lands on the App as a search does, which opens it in view.
            onPicked: id => Navigation.goToRow("apps", "appentry:" + id)
        }
        RowList {
            id: rows
            values: page.grouped
            delegate: AppRow {
                required property string key
                required property int index
                readonly property var row: rows.entry(key)
                width: ListView.view.width
                entry: row.value
                isType: row.isType
                nested: !row.isType
                detected: row.isType ? page.detectedIn(row.value.id) : []
                first: index === 0 && rows.first && !picker.open && !tester.open && !newType.open
                open: page.openId === landingId
                onToggled: page.toggle(landingId)
                onAdopted: app => page.adopt(app)
            }
        }
        Reveal {
            open: page.titleOnly.length > 0
            Column {
                width: parent.width
                Txt {
                    x: Theme.rowInset
                    width: parent.width - Theme.rowInset * 2
                    topPadding: Theme.space4
                    bottomPadding: Theme.space1
                    text: page.t.byTitleGroup
                    variant: "small"
                    tone: "secondary"
                }
                RowList {
                    id: titled
                    values: page.titleOnly
                    delegate: AppRow {
                        required property string key
                        readonly property var row: titled.entry(key)
                        width: ListView.view.width
                        entry: row.value
                        open: page.openId === landingId
                        onToggled: page.toggle(landingId)
                    }
                }
            }
        }
    }
}
