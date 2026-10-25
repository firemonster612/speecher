import QtQuick
import Speecher.Theme
import Speecher.Controls

// The mark and the window's name in the top left of the title strip, for the
// main window and setup alike. Put it at the window frame's top left: the
// mark sits titleBarInset from both edges, so its top and side margins are
// equal, and the words beside it are centred in the strip.
Item {
    id: brand

    property string title
    // A quieter line under the title, such as the version.
    property string detail

    implicitWidth: words.x + words.implicitWidth
    implicitHeight: Theme.titleBarHeight

    BrandMark {
        id: mark
        x: Theme.titleBarInset
        y: Theme.titleBarInset
        width: Theme.titleBarButton
        height: Theme.titleBarButton
    }
    Column {
        id: words
        x: mark.x + mark.width + 10
        anchors.verticalCenter: parent.verticalCenter
        Txt { text: brand.title; variant: "heading" }
        Txt { visible: text.length > 0; text: brand.detail; variant: "caption"; tone: "tertiary" }
    }
}
