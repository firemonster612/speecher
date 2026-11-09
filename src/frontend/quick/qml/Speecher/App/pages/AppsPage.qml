pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "apps"

// How Speecher writes and pastes in each app: the person's Apps, then the
// App Types every other app follows, each opening in place to the same
// editor. Add app opens the picker, closing any editor; an app picked there
// opens its editor. Test shows what Speecher makes of the app you switch to.
Page {
    id: page
    title: Schema.page("apps").title
    subtitle: Schema.page("apps").subtitle

    readonly property var t: Schema.apps.t
    // The App or App Type whose editor is open, by its landing id
    // ("appentry:<id>" or "apptype:<id>"); one at a time.
    property string openId: ""
    // Opening an editor closes the picker and Test, and opening either of
    // them closes the editor: one thing open at a time.
    function toggle(id) {
        openId = openId === id ? "" : id
        if (openId) { picker.close(); tester.close() }
    }

    Section {
        id: yours
        title: page.t.yourApps
        caption: page.t.yourAppsHelp
        actions: [
            Button {
                objectName: "testApp"
                text: page.t.test
                iconName: "scan-search"
                size: "small"
                variant: "ghost"
                onClicked: tester.start()
            },
            Button { text: page.t.addApp; iconName: "plus"; size: "small"; onClicked: picker.start() }
        ]
        // The History row is in each App's editor; a search for it lands on
        // the list.
        SearchLanding { rowIds: ["apps", "appHistory"]; target: yours }
        SearchLanding { rowIds: ["addApp"]; target: yours; onLanded: picker.start() }
        AccessibilityNotice { what: Schema.pasting.accessibilityUses[System.identifiesAppsWithoutAccessibility ? "inserting" : "apps"] }

        AppTest {
            id: tester
            onOpenChanged: if (open) { page.openId = ""; picker.close() }
        }
        AppPicker {
            id: picker
            onOpenChanged: if (open) { page.openId = ""; tester.close() }
            // Lands on the App as a search does, which opens it in view.
            onPicked: id => Navigation.goToRow("apps", "appentry:" + id)
        }
        RowList {
            id: apps
            values: Apps.entries
            delegate: AppRow {
                required property string key
                required property int index
                width: ListView.view.width
                entry: apps.entry(key)
                first: index === 0 && apps.first && !picker.open && !tester.open
                open: page.openId === landingId
                onToggled: page.toggle(landingId)
            }
        }
        Reveal {
            open: Apps.entries.length === 0 && !picker.open && !tester.open
            EmptyState { compact: true; title: page.t.noApps; text: page.t.noAppsHelp }
        }
    }

    Section {
        id: typesSection
        title: page.t.types
        caption: page.t.typesHelp(Apps.builtInCount)
        SearchLanding { rowIds: ["appTypes"]; target: typesSection }
        SearchLanding { rowIds: ["otherAppsProfile"]; target: typesSection; onLanded: page.openId = "apptype:general" }
        RowList {
            id: types
            values: Apps.types
            delegate: AppRow {
                required property string key
                required property int index
                width: ListView.view.width
                entry: types.entry(key)
                isType: true
                first: index === 0 && types.first
                open: page.openId === landingId
                onToggled: page.toggle(landingId)
            }
        }
    }
}
