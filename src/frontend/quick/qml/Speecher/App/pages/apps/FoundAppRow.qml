import QtQuick
import Speecher.Theme
import Speecher.Controls

// An app the platform found, installed or dictated into, as the picker and
// a type's editor list it to pick: its icon and name over ListRow's
// detail. app is { name, icon }; picking it is the row's click.
ListRow {
    id: row
    property var app: ({})
    implicitHeight: Math.max(Theme.rowHeight, words.implicitHeight + 18)
    clickable: true
    Accessible.name: app.name || ""
    AppIcon {
        id: appIcon
        anchors.verticalCenter: parent.verticalCenter
        name: row.app.name || ""
        source: row.app.icon ? "image://appicon/" + encodeURIComponent(row.app.icon) : ""
        size: 24
    }
    Column {
        id: words
        x: appIcon.width + Theme.space3
        width: parent.width - x
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Txt { width: parent.width; text: row.app.name || ""; variant: "label"; elide: Text.ElideRight }
        Txt { visible: text.length > 0; width: parent.width; text: row.detail; variant: "small"; tone: "secondary"; elide: Text.ElideRight }
    }
}
