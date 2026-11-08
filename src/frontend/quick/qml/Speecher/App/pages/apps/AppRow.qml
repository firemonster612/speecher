pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// An App or App Type on the Apps page: its icon, name and what it sets,
// opening in place to its editor. A search or a link from another page
// lands on it by "appentry:<id>" or "apptype:<id>" and opens it.
Column {
    id: root

    // An entry of Apps.entries, or of Apps.types for an App Type.
    property var entry: ({})
    property bool isType: false
    property bool open: false
    property bool first: Positioner.isFirstItem
    signal toggled()

    readonly property var t: Schema.apps.t
    readonly property var type: isType ? Schema.apps.type(entry.id) : null
    readonly property string landingId: (isType ? "apptype:" : "appentry:") + (entry.id || "")

    width: parent ? parent.width : implicitWidth

    ListRow {
        id: head
        readonly property string name: root.isType ? root.type.label : root.entry.name || ""
        readonly property string summary: root.isType ? Schema.apps.typeSummary(root.entry, id => Library.profileName(id))
                                                      : Schema.apps.summary(root.entry, id => Library.profileName(id))
        objectName: root.landingId
        first: root.first
        implicitHeight: Math.max(Theme.rowHeight, label.implicitHeight + 22)
        clickable: true
        Accessible.name: name
        Accessible.description: summary
        onClicked: root.toggled()
        AppIcon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            name: root.isType ? "" : head.name
            iconName: root.isType ? root.type.icon : ""
            source: !root.isType && root.entry.icon ? "image://appicon/" + encodeURIComponent(root.entry.icon) : ""
            size: 28
        }
        Column {
            id: label
            x: icon.width + Theme.space3
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Txt { width: parent.width; text: head.name; variant: "label"; elide: Text.ElideRight }
            Txt { width: parent.width; text: head.summary; variant: "small"; tone: "secondary"; elide: Text.ElideRight }
        }
        trailing: [
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-down"
                size: 16
                color: Theme.textSecondary
                rotation: root.open ? 180 : 0
                Behavior on rotation { Ease { duration: Theme.base } }
            }
        ]
        SearchLanding { rowIds: [root.landingId]; target: head; onLanded: if (!root.open) root.toggled() }
    }
    Reveal {
        open: root.open
        AppEditor { entry: root.entry; isType: root.isType }
    }
}
