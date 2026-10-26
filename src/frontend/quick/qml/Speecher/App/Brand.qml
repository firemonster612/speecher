import QtQuick
import Speecher.Theme
import Speecher.Controls

// The mark and the window's name in the top left of the title strip, for the
// main window and setup alike. Put it at the window frame's top left: the
// mark sits titleBarInset from both edges, so its top and side margins are
// equal, and the words beside it are centred in the strip. On macOS it
// starts after the traffic lights. Given a width, such as its column's, the
// words end titleBarInset inside it, and a detail too long for that loses
// its middle and shows in full under the pointer.
Item {
    id: brand

    property string title
    // A quieter line under the title, such as the version, and the whole of
    // it for its tooltip where the line is a short form.
    property string detail
    property string detailTip

    readonly property real _wordsWidth: Math.max(titleLine.implicitWidth, detailLine.implicitWidth)
    implicitWidth: words.x + _wordsWidth + Theme.titleBarInset
    implicitHeight: Theme.titleBarHeight

    BrandMark {
        id: mark
        x: Theme.titleBarLead + Theme.titleBarInset
        y: Theme.titleBarInset
        width: Theme.titleBarButton
        height: Theme.titleBarButton
    }
    Column {
        id: words
        x: mark.x + mark.width + 10
        width: Math.min(brand._wordsWidth, brand.width - x - Theme.titleBarInset)
        anchors.verticalCenter: parent.verticalCenter
        Txt { id: titleLine; text: brand.title; variant: "heading"; width: parent.width; elide: Text.ElideRight }
        Txt {
            id: detailLine
            visible: text.length > 0
            text: brand.detail
            variant: "caption"
            tone: "tertiary"
            width: parent.width
            elide: Text.ElideMiddle
            HoverHandler { id: detailHover; enabled: detailLine.truncated || brand.detailTip.length > 0 && brand.detailTip !== brand.detail }
            // Below, since the strip is at the window's top.
            Tip { text: brand.detailTip || brand.detail; shown: detailHover.hovered; y: detailLine.height + 6; transformOrigin: Item.Top }
        }
    }
}
