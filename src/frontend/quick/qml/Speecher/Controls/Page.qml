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
// page scrolls as a ListView (list) and builds only the rows near the view.
// The list places a row at its left edge, so a row spans the list and
// centres its content, contentWidth wide, to line up with the page's.
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
    property alias flickable: flick
    property alias list: flick
    property alias listModel: flick.model
    property alias listDelegate: flick.delegate
    // The model role whose changes start a section, which a row reads
    // through ListView.section, previousSection and nextSection.
    property alias listSection: flick.section.property
    readonly property alias contentWidth: column.width

    // Scrolls so item sits near the top of the view, for search jumps.
    function reveal(item) {
        const p = item.mapToItem(flick.contentItem, 0, 0)
        const to = Math.max(flick.originY, Math.min(p.y - 90, flick.originY + flick.contentHeight - flick.height))
        // Under reduced motion the page jumps there.
        if (Theme.movement === 0) { flick.contentY = to; return }
        scrollTo.to = to
        scrollTo.restart()
    }

    Ease { id: scrollTo; target: flick; property: "contentY"; duration: Theme.page; curve: Theme.easeInOut }

    // The page's content, which the list's header holds.
    Item {
        id: lead
        width: flick.width
        height: column.implicitHeight + 28

        Column {
            id: column
            width: Math.min(Theme.contentMaxWidth, flick.width - Theme.pageInset * 2)
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

    ListView {
        id: flick
        anchors.fill: parent
        objectName: "pageScroll"
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drags nothing on a desktop; touch still does.
        acceptedButtons: Qt.NoButton
        WheelScroll {}
        clip: true
        T.ScrollBar.vertical: ScrollBar {}
        // Up and Down belong to the page's controls, not the list.
        keyNavigationEnabled: false
        currentIndex: -1
        // A view's height of rows built past each edge.
        cacheBuffer: Math.max(0, height)
        header: Item {
            width: lead.width
            height: lead.height
            Component.onCompleted: lead.parent = this
        }
        footer: Item { width: 1; height: 44 }
    }
}
