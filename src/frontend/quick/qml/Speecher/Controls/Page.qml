pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A scrolling page: the title and a line of explanation, then sections of
// grouped rows. A page with no title brings its own heading. A page under
// others leads its title with the path to it, each step a link that asks
// for its page through pathPicked. A page a link opened has the way back
// above its title while backText says where to (backPicked).
//
// A page with more rows than it should build at once, such as History,
// lists them under its content through listModel and listDelegate: the
// page then scrolls as a ListView (list), its content the list's header,
// and builds only the rows near the view. The list places a row at its left
// edge, so a row spans the list and centres its content, columnWidth wide,
// to line up with the page's. Without a listModel the page scrolls as a
// Flickable: a ListView with no model goes back to its top whenever it lays
// out again.
Item {
    id: page

    property string title
    property string subtitle
    // The pages above this one, outermost first: [{ id, title }].
    property var path: []
    signal pathPicked(string pageId)
    property string backText
    signal backPicked()
    // The page's own search field while it shows one, or null: Ctrl+F puts
    // the cursor there, and opens the search palette without one.
    property Item findField: null
    // Kept while the link folds away, so its words don't vanish first.
    property string _shownBack
    onBackTextChanged: if (backText.length) _shownBack = backText
    default property alias content: body.data
    property alias headerActions: actions.data
    // Whether the page lists rows now, and so scrolls as its list.
    readonly property bool showsList: !!listDelegate && !!listModel
    readonly property Flickable flickable: showsList ? listView : plain
    readonly property alias list: listView
    property alias listModel: listView.model
    property alias listDelegate: listView.delegate
    // The model role whose changes start a section, which a row reads
    // through ListView.section, previousSection and nextSection.
    property alias listSection: listView.section.property
    readonly property alias columnWidth: column.width

    // Scrolls so item sits near the top of the view, for search jumps.
    function reveal(item) {
        const flick = flickable
        const p = item.mapToItem(flick.contentItem, 0, 0)
        const to = Math.max(flick.originY, Math.min(p.y - 90, flick.originY + flick.contentHeight - flick.height))
        // Under reduced motion the page jumps there.
        if (Theme.movement === 0) { flick.contentY = to; return }
        scrollTo.to = to
        scrollTo.restart()
    }

    Ease { id: scrollTo; target: page.flickable; property: "contentY"; duration: Theme.page; curve: Theme.easeInOut }

    // The page's content, which the Flickable holds, or the list's header.
    Item {
        id: lead
        width: page.width
        height: column.implicitHeight + 28

        Column {
            id: column
            width: Math.min(Theme.contentMaxWidth, page.width - Theme.pageInset * 2)
            x: (parent.width - width) / 2
            y: 28
            spacing: Theme.sectionGap

            Item {
                visible: page.title.length > 0
                width: parent.width
                height: Math.max(headText.implicitHeight, actions.childrenRect.height)
                Column {
                    id: headText
                    width: parent.width - actions.childrenRect.width - 16
                    spacing: 4
                    Reveal {
                        open: page.backText.length > 0
                        TextLink {
                            objectName: "backLink"
                            text: page._shownBack
                            iconName: "arrow-left"
                            onActivated: page.backPicked()
                        }
                        Item { width: 1; height: Theme.space2 }
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.space2
                        Repeater {
                            model: page.path
                            delegate: Row {
                                id: step
                                required property var modelData
                                spacing: Theme.space2
                                TextLink {
                                    text: step.modelData.title
                                    variant: "display"
                                    quiet: true
                                    onActivated: page.pathPicked(step.modelData.id)
                                }
                                Icon { name: "chevron-right"; size: 20; color: Theme.textTertiary; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                        Txt { text: page.title; variant: "display" }
                    }
                    Txt {
                        visible: page.subtitle.length > 0
                        text: page.subtitle
                        tone: "secondary"
                        width: Math.min(parent.width, 680)
                        wrapMode: Text.WordWrap
                    }
                }
                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 2
                    spacing: 8
                }
            }

            Column {
                id: body
                width: parent.width
                spacing: Theme.sectionGap
            }
        }
    }

    Flickable {
        id: plain
        visible: !page.showsList
        anchors.fill: parent
        contentWidth: width
        contentHeight: lead.height + 44
        objectName: visible ? "pageScroll" : ""
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drags nothing on a desktop; touch still does.
        acceptedButtons: Qt.NoButton
        WheelScroll {}
        clip: true
        T.ScrollBar.vertical: ScrollBar {}
    }

    ListView {
        id: listView
        visible: page.showsList
        anchors.fill: parent
        objectName: visible ? "pageScroll" : ""
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton
        WheelScroll {}
        clip: true
        T.ScrollBar.vertical: ScrollBar {}
        // Up and Down belong to the page's controls, not the list.
        keyNavigationEnabled: false
        currentIndex: -1
        // A view's height of rows built past each edge.
        cacheBuffer: Math.max(0, height)
        header: Item { width: lead.width }
        footer: Item { width: 1; height: 44 }
    }
    // The content goes to whichever scrolls the page.
    function _placeContent() {
        if (!showsList) {
            lead.parent = plain.contentItem
            return
        }
        // The list makes its header as it completes, which can be after
        // the page.
        if (!listView.headerItem) {
            Qt.callLater(_placeContent)
            return
        }
        listView.headerItem.height = lead.height
        lead.parent = listView.headerItem
    }
    Component.onCompleted: _placeContent()
    onShowsListChanged: _placeContent()
    // A list's header grows upward, moving what's above instead of the rows
    // below. Kept at the same distance from the top, the page grows as a
    // Flickable does: what's above stays put and what's below moves.
    Connections {
        target: page.showsList ? lead : null
        function onHeightChanged() {
            const shown = listView.contentY - listView.originY
            listView.headerItem.height = lead.height
            listView.forceLayout()
            listView.contentY = listView.originY + Math.max(0, Math.min(shown, listView.contentHeight - listView.height))
        }
    }
}
