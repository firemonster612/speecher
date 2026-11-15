pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// An App or App Type on the Apps page: its icon, name and what it sets,
// opening in place to its editor. An App listed under its type is nested,
// its icon in line with the type's name. A type's row names the apps
// Speecher puts in it that have nothing of their own. A search or a link
// from another page lands on it by "appentry:<id>" or "apptype:<id>" and
// opens it.
Column {
    id: root

    // An entry of Apps.entries, or of Apps.types for an App Type.
    property var entry: ({})
    property bool isType: false
    property bool nested: false
    // For a type: the apps Speecher puts in it with nothing of their own,
    // [{ name, icon, ids }], which its editor lists for adopted.
    property var detected: []
    property bool open: false
    property bool first: Positioner.isFirstItem
    signal toggled()
    signal adopted(var app)

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
            x: root.nested ? 28 + Theme.space3 : 0
            anchors.verticalCenter: parent.verticalCenter
            name: root.isType ? "" : head.name
            iconName: root.isType ? root.type.icon : ""
            source: !root.isType && root.entry.icon ? "image://appicon/" + encodeURIComponent(root.entry.icon) : ""
            size: root.nested ? 24 : 28
        }
        Column {
            id: label
            x: icon.x + icon.width + Theme.space3
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Txt { width: parent.width; text: head.name; variant: "label"; elide: Text.ElideRight }
            Txt { width: parent.width; text: head.summary; variant: "small"; tone: "secondary"; elide: Text.ElideRight }
        }
        trailing: [
            Txt {
                objectName: "appTypeDetected"
                visible: root.isType && !root.open && text.length > 0
                width: Math.min(implicitWidth, head.width / 3)
                anchors.verticalCenter: parent.verticalCenter
                text: root.detected.map(a => a.name).join(", ")
                variant: "small"
                tone: "tertiary"
                elide: Text.ElideRight
            },
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
        AppEditor {
            entry: root.entry
            isType: root.isType
            detected: root.detected
            onAdopted: app => root.adopted(app)
        }
    }
}
