import QtQuick
import Speecher.Theme
import Speecher.Controls

// The page a window shows beside its side column (the sidebar, or setup's
// steps), in either layout. Framed, it is a sheet inset under the title
// strip, 8px from the window's edges. Split, it runs edge to edge beside the
// column, divided from it by a rule, and its content starts under the strip.
// Switching eases from one to the other; content goes inside.
Item {
    id: sheet
    required property Item side
    default property alias content: body.data

    // 1 framed, 0 split, and between while the layout changes.
    property real framing: Theme.split ? 0 : 1
    Behavior on framing { enabled: Theme.movement > 0; Ease { duration: Theme.page; curve: Theme.easeInOut } }
    // Split, the sheet's outer corners are the window's.
    readonly property real _windowRadius: (Window.window as ClientWindow)?.cornerRadius ?? 0
    readonly property real _outerRadius: Theme.radiusPanel * framing + _windowRadius * (1 - framing)

    anchors.left: side.right
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.bottom: parent.bottom

    Rectangle {
        id: surface
        anchors.fill: parent
        anchors.topMargin: Theme.titleBarHeight * sheet.framing
        anchors.rightMargin: 8 * sheet.framing
        anchors.bottomMargin: 8 * sheet.framing
        topLeftRadius: Theme.radiusPanel * sheet.framing
        bottomLeftRadius: Theme.radiusPanel * sheet.framing
        topRightRadius: sheet._outerRadius
        bottomRightRadius: sheet._outerRadius
        color: Theme.sheet
        border.width: 1
        border.color: Qt.rgba(Theme.sheetBorder.r, Theme.sheetBorder.g, Theme.sheetBorder.b, Theme.sheetBorder.a * sheet.framing)
    }
    // The rule between the column and the page, split.
    Rectangle {
        width: 1
        height: parent.height
        color: Theme.sheetBorder
        opacity: 1 - sheet.framing
    }

    Item {
        id: body
        anchors.fill: surface
        anchors.topMargin: Theme.titleBarHeight * (1 - sheet.framing)
    }
}
