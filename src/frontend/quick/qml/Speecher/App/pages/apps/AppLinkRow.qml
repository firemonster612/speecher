import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend

// An App or App Type listed on another page, opening it in Apps: its icon
// and name over ListRow's detail, with a chevron. entry is
// Schema.apps.typeLink's or appLink's { id, name, typeIcon, icon }; detail
// wraps, so a long one shows whole. It draws its own words, as ListRow's
// have no app icon, so it leaves title empty.
ListRow {
    id: row
    property var entry: ({})

    implicitHeight: Math.max(Theme.rowHeight, words.implicitHeight + 22)
    clickable: true
    Accessible.name: entry.name
    onClicked: Navigation.follow("apps", entry.id)
    AppIcon {
        id: icon
        anchors.verticalCenter: parent.verticalCenter
        name: row.entry.typeIcon ? "" : row.entry.name
        iconName: row.entry.typeIcon
        source: row.entry.icon ? "image://appicon/" + encodeURIComponent(row.entry.icon) : ""
        size: 28
    }
    Column {
        id: words
        x: icon.width + Theme.space3
        width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        Txt { width: parent.width; text: row.entry.name; variant: "label"; elide: Text.ElideRight }
        Txt {
            objectName: "appLinkDetail"
            width: parent.width
            text: row.detail
            variant: "small"
            tone: "secondary"
            wrapMode: Text.WordWrap
        }
    }
    trailing: [ Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevron-right"; size: 16 } ]
}
