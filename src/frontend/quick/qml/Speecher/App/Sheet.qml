import QtQuick
import Speecher.Theme

// The inset sheet a window shows its pages on, filling the window right of
// a side column (the sidebar, or setup's steps) with an 8px margin.
Rectangle {
    required property Item side

    anchors.left: side.right
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    anchors.margins: 8
    anchors.leftMargin: 0
    radius: Theme.radiusPanel
    color: Theme.sheet
    border.width: 1
    border.color: Theme.sheetBorder
}
