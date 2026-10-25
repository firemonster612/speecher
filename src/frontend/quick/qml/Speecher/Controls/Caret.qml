import QtQuick
import Speecher.Theme

// The caret where the next words land: a blue pencil line after the last
// character of a TextEdit, blinking slowly. Declare it inside the edit.
Rectangle {
    required property TextEdit edit
    // Read after layout: the end moves when the text wraps.
    readonly property rect _end: edit.contentHeight >= 0 && edit.width > 0 ? edit.positionToRectangle(edit.length) : Qt.rect(0, 0, 0, 0)

    x: _end.x + 2
    y: _end.y + 1
    width: 2
    height: Math.max(0, _end.height - 2)
    radius: 1
    color: Theme.accent
    SequentialAnimation on opacity {
        loops: Animation.Infinite
        Ease { to: 0.15; duration: Theme.ms(520); curve: Theme.breathe }
        Ease { to: 1; duration: Theme.ms(520); curve: Theme.breathe }
    }
}
