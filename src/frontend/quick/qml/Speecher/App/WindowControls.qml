import QtQuick
import QtQuick.Window
import Speecher.Theme
import Speecher.Controls
import Speecher.Wording

// Minimize, maximize or restore, and close, for a window that draws its own
// frame.
Row {
    id: controls
    required property Window window
    readonly property bool maximized: window.visibility === Window.Maximized

    spacing: 2

    IconButton {
        size: Theme.titleBarButton
        iconName: "minus"
        tooltip: Words.shell.minimize
        focusPolicy: Qt.NoFocus
        onClicked: controls.window.showMinimized()
    }
    IconButton {
        size: Theme.titleBarButton
        iconName: controls.maximized ? "copy" : "square"
        tooltip: controls.maximized ? Words.shell.restore : Words.shell.maximize
        focusPolicy: Qt.NoFocus
        onClicked: controls.maximized ? controls.window.showNormal() : controls.window.showMaximized()
    }
    IconButton {
        size: Theme.titleBarButton
        iconName: "x"
        tooltip: Words.shell.close
        focusPolicy: Qt.NoFocus
        iconColor: hovered ? Theme.negative : Theme.textSecondary
        onClicked: controls.window.close()
    }
}
