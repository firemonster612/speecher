pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Add app: a search over the apps dictated into lately and the installed
// apps, then two ways to add one the lists miss: switching to it, and
// matching it by name or window title. Picking an app that has an App
// already opens that one; picked carries the App's id either way.
Reveal {
    id: picker

    signal picked(string id)

    readonly property var t: Schema.apps.t
    readonly property string query: search.text.trim().toLowerCase()
    // At most this many installed apps show; typing narrows the rest.
    readonly property int shownInstalled: 40
    readonly property var recent: Apps.recent.filter(a => _found(a.name, [a.appId]))
    readonly property var installedFound: Apps.installed.filter(a => _found(a.name, a.ids))
    property bool matching: false
    property string matchKind: "name"

    function start() {
        Apps.refresh()
        search.text = ""
        matching = false
        Apps.cancelCapture()
        open = true
        search.forceActiveFocus()
    }
    function close() { Apps.cancelCapture(); open = false }
    function _found(name, ids) {
        return !query || name.toLowerCase().includes(query) || ids.some(id => id.toLowerCase().includes(query))
    }
    function _choose(entry, app) {
        const id = entry || Apps.add(app).id
        if (id) { close(); picked(id) }
    }
    // What a listed app gets now, where that is not what any other app gets.
    function _detail(app) {
        if (app.entry) return t.hasSettings
        const type = app.type !== "general" && app.type !== "unknown" ? Schema.apps.type(app.type).one : ""
        const profile = app.profile !== (Apps.types.find(o => o.id === "general") || {}).profile ? Library.profileName(app.profile) + " profile" : ""
        return [type, profile].filter(s => s).join(", ")
    }

    Connections {
        target: Apps
        function onCaptured(id) { picker.close(); picker.picked(id) }
    }

    Item {
        width: parent.width
        implicitHeight: body.implicitHeight + Theme.space5 * 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: Theme.washInset
            radius: Theme.washRadius
            color: Theme.fill
        }
        Column {
            id: body
            x: Theme.rowInset
            y: Theme.space5
            width: parent.width - Theme.rowInset * 2
            spacing: Theme.space3

            Item {
                width: parent.width
                height: search.height
                Txt { id: heading; anchors.verticalCenter: parent.verticalCenter; text: picker.t.addApp; variant: "heading" }
                SearchField {
                    id: search
                    x: heading.width + Theme.space4
                    width: parent.width - x - cancel.width - Theme.space2
                    clearText: Words.clear
                    placeholderText: picker.t.search
                }
                Button { id: cancel; anchors.right: parent.right; text: Words.cancel; variant: "ghost"; onClicked: picker.close() }
            }

            Txt { visible: picker.recent.length > 0; text: picker.t.recent; variant: "small"; tone: "secondary" }
            RowList {
                id: recentList
                width: parent.width
                values: picker.recent
                keyOf: a => a.appId
                delegate: PickerRow {
                    required property string key
                    required property int index
                    app: recentList.entry(key)
                    first: index === 0
                    detail: picker._detail(app)
                    onClicked: picker._choose(app.entry, { name: app.name, icon: app.icon, ids: [app.appId] })
                }
            }

            Txt { text: picker.t.installed; variant: "small"; tone: "secondary" }
            Column {
                    id: installedColumn
                    width: parent.width
                    RowList {
                        id: installedList
                        width: parent.width
                        values: picker.installedFound.slice(0, picker.shownInstalled)
                        keyOf: a => a.ids.join("|")
                        delegate: PickerRow {
                            required property string key
                            required property int index
                            app: installedList.entry(key)
                            first: index === 0
                            detail: picker._detail(app)
                            onClicked: picker._choose(app.entry, { name: app.name, icon: app.icon, ids: app.ids })
                        }
                    }
                    Reveal {
                        open: Apps.scanning || picker.installedFound.length === 0
                        EmptyState { compact: true; title: Apps.scanning ? picker.t.scanning : picker.t.noMatch }
                    }
                    Reveal {
                        open: picker.installedFound.length > picker.shownInstalled
                        Txt {
                            x: Theme.rowInset
                            topPadding: Theme.space2
                            bottomPadding: Theme.space2
                            text: picker.t.moreInstalled(picker.installedFound.length - picker.shownInstalled)
                            variant: "small"
                            tone: "secondary"
                        }
                    }
            }

            Txt { text: picker.t.notListed; variant: "small"; tone: "secondary" }
            Row {
                width: parent.width
                spacing: Theme.space3
                Button {
                    id: switchButton
                    text: picker.t.switchToIt
                    iconName: "mouse-pointer-click"
                    busy: Apps.capture === "waiting"
                    onClicked: Apps.capture === "waiting" ? Apps.cancelCapture() : Apps.captureNext(5)
                }
                Txt {
                    width: parent.width - switchButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: Apps.capture === "waiting" ? picker.t.switching(Apps.captureCountdown)
                        : Apps.capture === "failed" ? picker.t.switchFailed : picker.t.switchHelp
                    variant: "small"
                    tone: Apps.capture === "failed" ? "negative" : "secondary"
                    wrapMode: Text.WordWrap
                }
            }
            Row {
                width: parent.width
                spacing: Theme.space3
                Button {
                    id: matchButton
                    text: picker.t.matchByText
                    iconName: "text-search"
                    onClicked: { picker.matching = !picker.matching; matchError.text = "" }
                }
                Txt {
                    width: parent.width - matchButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: picker.t.matchHelp
                    variant: "small"
                    tone: "secondary"
                    wrapMode: Text.WordWrap
                }
            }
            Reveal {
                open: picker.matching
                width: parent.width
                Column {
                    width: parent.width
                    spacing: Theme.space2
                    Row {
                        width: parent.width
                        spacing: Theme.space3
                        SegmentedControl {
                            id: kind
                            options: [{ id: "name", label: picker.t.byName }, { id: "title", label: picker.t.byTitle }]
                            currentId: picker.matchKind
                            onActivated: id => picker.matchKind = id
                        }
                        TextField {
                            id: matchText
                            width: parent.width - kind.width - addMatch.width - parent.spacing * 2
                            placeholderText: picker.t.matchPlaceholder
                            onAccepted: addMatch.clicked()
                        }
                        Button {
                            id: addMatch
                            text: picker.t.add
                            enabled: matchText.text.trim().length > 0
                            onClicked: {
                                const added = Apps.add({ name: matchText.text.trim(), matches: [{ kind: picker.matchKind, value: matchText.text.trim() }] })
                                matchError.text = Schema.apps.refusals[added.refusal] || ""
                                if (added.id) { matchText.text = ""; picker.close(); picker.picked(added.id) }
                            }
                        }
                    }
                    Txt { id: matchError; visible: text.length > 0; variant: "small"; tone: "negative"; width: parent.width; wrapMode: Text.WordWrap }
                }
            }
        }
    }

    // One app the picker lists: its icon, name and what it gets now.
    component PickerRow: ListRow {
        id: pickerRow
        property var app: ({})
        implicitHeight: Math.max(Theme.rowHeight, words.implicitHeight + 18)
        clickable: true
        Accessible.name: app.name || ""
        AppIcon {
            id: appIcon
            anchors.verticalCenter: parent.verticalCenter
            name: pickerRow.app.name || ""
            source: pickerRow.app.icon ? "image://appicon/" + encodeURIComponent(pickerRow.app.icon) : ""
            size: 24
        }
        Column {
            id: words
            x: appIcon.width + Theme.space3
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Txt { width: parent.width; text: pickerRow.app.name || ""; variant: "label"; elide: Text.ElideRight }
            Txt { visible: text.length > 0; width: parent.width; text: pickerRow.detail; variant: "small"; tone: "secondary"; elide: Text.ElideRight }
        }
    }
}
