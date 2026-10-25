import QtQuick
import QtQuick.Templates as T
import Speecher.Theme

// A multi-line text field that grows with its text. mono sets it in the
// monospace face for prompts and snippets.
T.TextArea {
    id: control

    property bool mono: false
    property int minHeight: 76

    implicitWidth: 320
    implicitHeight: Math.max(minHeight, contentHeight + topPadding + bottomPadding)
    leftPadding: 11
    rightPadding: 11
    topPadding: 9
    bottomPadding: 9
    wrapMode: TextEdit.Wrap
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accentRing
    selectedTextColor: Theme.text
    font.family: mono ? Theme.monoFamily : Theme.fontFamily
    font.pixelSize: mono ? Theme.fontSmall : Theme.fontBody
    hoverEnabled: true
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    Grow on implicitHeight { duration: Theme.base }

    // Templates draws no placeholder of its own.
    Txt {
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        text: control.placeholderText
        visible: !control.length && !control.preeditText
        tone: "tertiary"
        font: control.font
        wrapMode: Text.WordWrap
        elide: Text.ElideRight
    }

    background: InputFrame { focused: control.activeFocus; hovered: control.hovered }
}
