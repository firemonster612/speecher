import QtQuick
import Speecher.Controls
import Speecher.Wording

// A card of rows leading to a page's subpages, each with its title and the
// line under it, so the page offers them as well as the sidebar.
Section {
    id: links
    required property string pageId

    Repeater {
        model: Schema.subpages(links.pageId)
        SettingRow {
            required property var modelData
            title: modelData.title
            help: modelData.subtitle || ""
            clickable: true
            onClicked: Navigation.go(modelData.id)
        }
    }
}
