pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The window's sidebar: the app mark, search, the pages in groups with a
// selection that glides between them, and dictation status at the bottom.
// Tab reaches search, the pages and the update note; up and down arrows move
// between the pages that show, Right and Left open and close a page's
// subpages, and Return or Space opens one.
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
    // In the window's title strip, which lies over it.
    Brand {
        id: brand
        width: parent.width
        title: Words.shell.appName
        detail: Words.version(Status.version)
    }

    // Search --------------------------------------------------------------
    Rectangle {
        id: search
        x: 12
        y: brand.height + Theme.space2
        width: parent.width - 24
        height: Theme.controlHeight
        radius: Theme.radius
        color: searchTap.pressed ? Theme.fillStrong : searchHover.hovered ? Theme.fill : "transparent"
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
        // Qt reads Ctrl in a shortcut as ⌘ on macOS.
        KeyCombo { keys: [Qt.platform.os === "osx" ? "⌘" : "Ctrl", "K"]; size: "small"; anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter }
        HoverHandler { id: searchHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: searchTap; onTapped: { searchFocus.pointed(); sidebar.searchRequested() } }
        activeFocusOnTab: true
        Keys.onReturnPressed: { searchFocus.keyed(); sidebar.searchRequested() }
        Keys.onSpacePressed: { searchFocus.keyed(); sidebar.searchRequested() }
        FocusVisible { id: searchFocus }
        Accessible.role: Accessible.Button
        Accessible.name: Words.shell.searchSettings
        Accessible.onPressAction: sidebar.searchRequested()
        FocusRing { target: search; shown: searchFocus.shown }
    }

    // Pages ---------------------------------------------------------------
    // One page in the list, set in by indent under its parent when it is a
    // subpage. A page with subpages has a chevron that opens and closes
    // them, as Right and Left do from the keyboard; it asks through
    // openRequested and shows open.
    component Entry: Item {
        id: entry
        required property var page
        // Whether the pill sits here: on the current page, or on the parent
        // of a current subpage that is folded away.
        required property bool marked
        property int indent: 0
        property bool hasSubpages: false
        property bool open: false
        signal openRequested(bool open)

        x: 12
        width: nav.width - 24
        height: Theme.controlHeight
        onMarkedChanged: if (marked) selection.moveTo(entry)
        Component.onCompleted: if (marked) selection.moveTo(entry)
        activeFocusOnTab: true
        onActiveFocusChanged: if (activeFocus) sidebar._keepInView(entry)
        Keys.onReturnPressed: { entryFocus.keyed(); Navigation.go(entry.page.id) }
        Keys.onSpacePressed: { entryFocus.keyed(); Navigation.go(entry.page.id) }
        Keys.onUpPressed: sidebar._step(entry, false)
        Keys.onDownPressed: sidebar._step(entry, true)
        Keys.onRightPressed: { entryFocus.keyed(); if (hasSubpages) openRequested(true) }
        Keys.onLeftPressed: { entryFocus.keyed(); if (hasSubpages) openRequested(false) }
        FocusVisible { id: entryFocus }
        Accessible.role: Accessible.PageTab
        Accessible.name: entry.page.title
        Accessible.selected: entry.marked
        Accessible.onPressAction: Navigation.go(entry.page.id)

        Rectangle {
            id: entryWash
            anchors.fill: parent
            radius: Theme.radius
            color: entry.marked ? "transparent" : entryTap.pressed ? Theme.fillStrong : entryHover.hovered ? Theme.fill : "transparent"
            Behavior on color { EaseColor {} }
        }
        FocusRing { target: entryWash; shown: entryFocus.shown }
        Row {
            x: 10 + entry.indent
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Icon {
                name: entry.page.icon
                size: 16
                color: entry.marked || entryHover.hovered ? Theme.text : Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
                Behavior on color { EaseColor {} }
            }
            Txt {
                text: entry.page.title
                variant: "label"
                strong: entry.marked
                tone: entry.marked || entryHover.hovered ? "primary" : "secondary"
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        HoverHandler { id: entryHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: entryTap; onTapped: Navigation.go(entry.page.id) }

        // Its own target, so opening the subpages leaves the page where it is.
        Item {
            visible: entry.hasSubpages
            anchors.right: parent.right
            width: Theme.controlHeight
            height: parent.height
            Icon {
                anchors.centerIn: parent
                name: "chevron-down"
                size: 14
                rotation: entry.open ? 0 : -90
                color: chevronHover.hovered ? Theme.text : Theme.textTertiary
                Behavior on rotation { Ease { duration: Theme.base } }
                Behavior on color { EaseColor {} }
            }
            HoverHandler { id: chevronHover }
            // Takes the tap from the entry's own handler.
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: entry.openRequested(!entry.open) }
            Accessible.role: Accessible.Button
            Accessible.ignored: !entry.hasSubpages
            Accessible.name: entry.open ? Words.shell.hideSubpages : Words.shell.showSubpages
            Accessible.onPressAction: entry.openRequested(!entry.open)
        }
    }

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

        // One selection pill that glides to the marked entry. It follows
        // that entry at once as subpages open and close above it; only the
        // glide from the entry before eases, so a move during one lands.
        Rectangle {
            id: selection
            property Item target: null
            // The target's place in the list, summed through its columns so
            // every layout change reaches it and a Reveal's drop-in does not.
            readonly property real targetY: {
                let y = 0
                for (let p = target; p && p !== nav; p = p.parent) y += p.y
                return y
            }
            // What is left of the glide from the entry before.
            property real glide: 0
            x: 12
            width: nav.width - 24
            y: targetY + glide
            height: target ? target.height : 0
            radius: Theme.radius
            color: Theme.sheet
            border.width: 1
            border.color: Theme.border
            visible: target !== null

            function moveTo(item) {
                const from = y
                target = item
                // On the first layout, and under reduced motion, it moves at once.
                if (!settle.done || Theme.movement === 0) return
                glide = from - targetY
                glideHome.restart()
            }
            Ease { id: glideHome; target: selection; property: "glide"; to: 0; duration: Theme.slow; curve: Theme.springy }
            Settle { id: settle }
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
                        x: Theme.sideInset
                        text: group.modelData.title
                        variant: "small"
                        strong: true
                        tone: "tertiary"
                        bottomPadding: 6
                    }
                    // A page and its subpages, which open under it when it
                    // or one of them is opened and stay open after. Closed
                    // by hand, they stay closed until opened again or one of
                    // them is opened.
                    Repeater {
                        model: Schema.pages.filter(p => p.group === group.modelData.id)
                        delegate: Column {
                            id: family
                            required property var modelData
                            readonly property var subpages: Schema.subpages(modelData.id)
                            readonly property bool subpageCurrent: subpages.some(p => p.id === Navigation.current)
                            property bool open: false
                            property bool closedByHand: false
                            function follow() {
                                if (subpageCurrent || Navigation.current === modelData.id && !closedByHand) open = true
                            }
                            Component.onCompleted: follow()
                            Connections { target: Navigation; function onCurrentChanged() { family.follow() } }
                            width: nav.width

                            Entry {
                                page: family.modelData
                                marked: Navigation.current === family.modelData.id || family.subpageCurrent && !family.open
                                hasSubpages: family.subpages.length > 0
                                open: family.open
                                onOpenRequested: wanted => { family.open = wanted; family.closedByHand = !wanted }
                            }
                            Reveal {
                                open: family.open
                                Item {
                                    width: parent.width
                                    height: subpageList.height
                                    // The guide from the parent's icon down its subpages.
                                    Rectangle { x: 30; y: 5; width: 1; height: parent.height - 9; color: Theme.rule }
                                    Column {
                                        id: subpageList
                                        width: parent.width
                                        topPadding: 1
                                        spacing: 1
                                        Repeater {
                                            model: family.subpages
                                            delegate: Entry {
                                                required property var modelData
                                                page: modelData
                                                marked: Navigation.current === modelData.id && family.open
                                                indent: 26
                                            }
                                        }
                                    }
                                }
                            }
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
        x: Theme.sideInset
        width: parent.width - Theme.sideInset * 2
        open: Updates.state === "ready" && Updates.attention

        TextLink {
            iconName: "arrow-down-to-line"
            text: Words.updateReady(Updates.availableVersion)
            variant: "labelSmall"
            onActivated: Navigation.go("updates")
        }
    }
}
