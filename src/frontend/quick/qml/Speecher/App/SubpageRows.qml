pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// A card of rows leading to a page's subpages, each with its title and the
// line under it, so the page offers them as well as the sidebar.
Section {
    id: links
    required property string pageId
    // A line for a subpage by id, such as how it is set now, in place of its
    // subtitle: { shortcuts: "Ctrl+Space starts a dictation" }.
    property var lines: ({})

    Repeater {
        model: Schema.subpages(links.pageId)
        SettingRow {
            required property var modelData
            title: modelData.title
            help: links.lines[modelData.id] ?? modelData.subtitle ?? ""
            clickable: true
            onClicked: Navigation.go(modelData.id)
        }
    }
}
