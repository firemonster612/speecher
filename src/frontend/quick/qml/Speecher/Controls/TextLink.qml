import QtQuick
import Speecher.Theme

// A pencil link in or beside a line of text, with an optional icon before
// it: underlined under the pointer, darker while pressed, and a link to the
// keyboard too (Tab reaches it, Return or Space follows it). A quiet link
// is in secondary ink that darkens under the pointer, for a page's path in
// its title, where a heading would read as one word in pencil.
Item {
    id: link

    property string text
    property string iconName
    property string variant: "small"
    property bool quiet: false
    signal activated()

    readonly property color _ink: quiet ? (hover.hovered || tap.pressed ? Theme.text : Theme.textSecondary)
                                        : tap.pressed ? Theme.accentHover : Theme.accent

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
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
