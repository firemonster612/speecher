pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The window's sidebar: the app mark, search, the pages in groups with a
// selection that glides between them, and dictation status at the bottom.
// Tab reaches search, the pages and the update note; up and down arrows move
// between pages and Return or Space opens one.
Item {
    id: sidebar
    signal searchRequested()

    // Moves focus to the page before or after item, staying on the list.
    function _step(item, forward) {
        const next = item.nextItemInFocusChain(forward)
        for (let p = next; p; p = p.parent)
            if (p === nav) { next.forceActiveFocus(Qt.TabFocusReason); return }
    }
    // Scrolls the page list just enough to show item, which has focus.
    function _keepInView(item) {
        const top = item.mapToItem(nav, 0, 0).y
        const y = Math.min(navFlick.contentY, top)
        navFlick.contentY = Math.max(0, Math.max(y, top + item.height - navFlick.height))
    }

    // App mark ------------------------------------------------------------
    Row {
        id: brand
        x: 18
        // Centred in the window's title strip, which lies over it.
        y: (Theme.titleBarHeight - height) / 2
        spacing: 10
        BrandMark {}
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Txt { text: Words.shell.appName; variant: "heading" }
            Txt { text: Words.version(Status.version); variant: "caption"; tone: "tertiary" }
        }
    }

    // Search --------------------------------------------------------------
    Rectangle {
        id: search
        x: 12
        y: brand.y + brand.height + 18
        width: parent.width - 24
        height: Theme.controlHeight
        radius: Theme.radius
        color: searchHover.hovered ? Theme.sheet : Qt.rgba(Theme.sheet.r, Theme.sheet.g, Theme.sheet.b, 0.55)
        border.width: 1
        border.color: Theme.border
        Behavior on color { EaseColor {} }
        Row {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Icon { name: "search"; size: 14; color: Theme.textTertiary; anchors.verticalCenter: parent.verticalCenter }
            Txt { text: Words.shell.searchSettings; tone: "tertiary"; anchors.verticalCenter: parent.verticalCenter }
        }
        KeyCombo { keys: ["Ctrl", "K"]; size: "small"; anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter }
        HoverHandler { id: searchHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: sidebar.searchRequested() }
        activeFocusOnTab: true
        Keys.onReturnPressed: sidebar.searchRequested()
        Keys.onSpacePressed: sidebar.searchRequested()
        Accessible.role: Accessible.Button
        Accessible.name: Words.shell.searchSettings
        Accessible.onPressAction: sidebar.searchRequested()
        FocusRing { target: search; shown: search.activeFocus }
    }

    // Pages ---------------------------------------------------------------
    Flickable {
        id: navFlick
        anchors.top: search.bottom
        anchors.topMargin: 14
        anchors.bottom: footer.top
        anchors.bottomMargin: 8
        width: parent.width
        contentHeight: nav.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drags nothing on a desktop; touch still does.
        acceptedButtons: Qt.NoButton
        WheelScroll {}
        clip: true

        // One selection pill that moves to the current item.
        Rectangle {
            id: selection
            property Item target: null
            x: 12
            width: nav.width - 24
            // Reading nav.height re-runs this once the column has laid out.
            y: target && nav.height > 0 ? target.mapToItem(nav, 0, 0).y : 0
            height: target ? target.height : 0
            radius: Theme.radius
            color: Theme.sheet
            border.width: 1
            border.color: Theme.border
            visible: target !== null
            // Under reduced motion it moves at once.
            Behavior on y { enabled: selection.ready && Theme.movement > 0; Ease { duration: Theme.slow; curve: Theme.springy } }
            property bool ready: false
            Component.onCompleted: Qt.callLater(() => ready = true)
        }

        Column {
            id: nav
            width: parent.width
            spacing: 2
            Repeater {
                model: Schema.groups
                delegate: Column {
                    id: group
                    required property var modelData
                    width: nav.width
                    spacing: 1
                    topPadding: group.modelData.title.length ? 16 : 0

                    Txt {
                        visible: group.modelData.title.length > 0
                        x: 22
                        text: group.modelData.title
                        variant: "small"
                        strong: true
                        tone: "tertiary"
                        bottomPadding: 6
                    }
                    Repeater {
                        model: Schema.pages.filter(p => p.group === group.modelData.id)
                        delegate: Item {
                            id: entry
                            required property var modelData
                            readonly property bool current: Navigation.current === entry.modelData.id
                            x: 12
                            width: nav.width - 24
                            height: Theme.controlHeight
                            onCurrentChanged: if (current) selection.target = entry
                            Component.onCompleted: if (current) selection.target = entry
                            activeFocusOnTab: true
                            onActiveFocusChanged: if (activeFocus) sidebar._keepInView(entry)
                            Keys.onReturnPressed: Navigation.go(entry.modelData.id)
                            Keys.onSpacePressed: Navigation.go(entry.modelData.id)
                            Keys.onUpPressed: sidebar._step(entry, false)
                            Keys.onDownPressed: sidebar._step(entry, true)
                            Accessible.role: Accessible.PageTab
                            Accessible.name: entry.modelData.title
                            Accessible.selected: entry.current
                            Accessible.onPressAction: Navigation.go(entry.modelData.id)

                            Rectangle {
                                id: entryWash
                                anchors.fill: parent
                                radius: Theme.radius
                                color: entryHover.hovered && !entry.current ? Theme.fill : "transparent"
                                Behavior on color { EaseColor {} }
                            }
                            FocusRing { target: entryWash; shown: entry.activeFocus }
                            Row {
                                x: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                Icon {
                                    name: entry.modelData.icon
                                    size: 16
                                    color: entry.current || entryHover.hovered ? Theme.text : Theme.textSecondary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Txt {
                                    text: entry.modelData.title
                                    variant: "label"
                                    strong: entry.current
                                    tone: entry.current || entryHover.hovered ? "primary" : "secondary"
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            HoverHandler { id: entryHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: Navigation.go(entry.modelData.id) }
                        }
                    }
                }
            }
        }
    }

    // Status --------------------------------------------------------------
    Reveal {
        id: footer
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        x: 22
        width: parent.width - 44
        open: Status.updateReady

        Item {
            id: update
            implicitWidth: updateRow.implicitWidth
            implicitHeight: updateRow.implicitHeight
            activeFocusOnTab: true
            Keys.onReturnPressed: Navigation.go("updates")
            Keys.onSpacePressed: Navigation.go("updates")
            Accessible.role: Accessible.Link
            Accessible.name: Words.updateReady(Status.updateVersion)
            Accessible.onPressAction: Navigation.go("updates")
            Row {
                id: updateRow
                spacing: 8
                Icon { name: "arrow-down-to-line"; size: 15; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                Txt {
                    text: Words.updateReady(Status.updateVersion)
                    variant: "labelSmall"
                    tone: "accent"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: Navigation.go("updates") }
            FocusRing { target: update; shown: update.activeFocus; targetRadius: Theme.radiusSmall }
        }
    }
}
