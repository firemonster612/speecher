import QtQuick
import Speecher.Theme

// A pencil link in or beside a line of text, with an optional icon before
// it: underlined under the pointer, darker while pressed, and a link to the
// keyboard too (Tab reaches it, Return or Space follows it).
Item {
    id: link

    property string text
    property string iconName
    property string variant: "small"
    signal activated()

    readonly property color _ink: tap.pressed ? Theme.accentHover : Theme.accent

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Link
    Accessible.name: text
    Accessible.onPressAction: activated()
    Keys.onReturnPressed: { focusVisible.keyed(); activated() }
    Keys.onSpacePressed: { focusVisible.keyed(); activated() }
    FocusVisible { id: focusVisible }

    Row {
        id: row
        spacing: Theme.space2
        Icon {
            visible: link.iconName.length > 0
            name: link.iconName
            size: 15
            color: link._ink
            anchors.verticalCenter: parent.verticalCenter
        }
        Txt {
            text: link.text
            variant: link.variant
            color: link._ink
            font.underline: hover.hovered
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    FocusRing { target: link; shown: focusVisible.shown; targetRadius: Theme.radiusSmall }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; onTapped: { focusVisible.pointed(); link.activated() } }
}
