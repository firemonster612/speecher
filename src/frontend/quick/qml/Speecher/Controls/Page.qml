pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A scrolling page: the title and a line of explanation, then sections of
// grouped rows. A page with no title brings its own heading. A page under
// others leads its title with the path to it, each step a link that asks
// for its page through pathPicked.
Item {
    id: page

    property string title
    property string subtitle
    // The pages above this one, outermost first: [{ id, title }].
    property var path: []
    signal pathPicked(string pageId)
    default property alias content: body.data
    property alias headerActions: actions.data
    property alias flickable: flick

    // Scrolls so item sits near the top of the view, for search jumps.
    function reveal(item) {
        const p = item.mapToItem(flick.contentItem, 0, 0)
        const to = Math.max(0, Math.min(p.y - 90, flick.contentHeight - flick.height))
        // Under reduced motion the page jumps there.
        if (Theme.movement === 0) { flick.contentY = to; return }
        scrollTo.to = to
        scrollTo.restart()
    }

    Ease { id: scrollTo; target: flick; property: "contentY"; duration: Theme.page; curve: Theme.easeInOut }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 72
        objectName: "pageScroll"
        boundsBehavior: Flickable.StopAtBounds
        // A mouse drags nothing on a desktop; touch still does.
        acceptedButtons: Qt.NoButton
        WheelScroll {}
        clip: true
        T.ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            width: Math.min(Theme.contentMaxWidth, flick.width - 64)
            x: (flick.width - width) / 2
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
}
