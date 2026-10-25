import QtQuick
import Speecher.Theme

// One card of a whole-card choice, the shell OptionCards and ServiceCards
// share: the chosen card takes a pencil wash, outline and check, the others
// lighten under the pointer, and every card gives a little when pressed.
// Content goes inside, laid out within padding; the check sits in the top
// right corner, so leave room for it beside the first line.
Rectangle {
    id: card

    property bool selected: false
    property bool focused: false
    property int padding: 14
    default property alias content: body.data
    signal activated()

    radius: Theme.radiusLarge
    color: selected ? Theme.accentSoft : tap.pressed ? Theme.fillStrong : hover.hovered ? Theme.fill : "transparent"
    border.width: 1
    border.color: selected ? Theme.accent : hover.hovered ? Theme.borderStrong : Theme.border
    scale: tap.pressed ? Theme.motionScale(0.98) : 1
    Behavior on color { EaseColor {} }
    Behavior on border.color { EaseColor {} }
    Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }
    Accessible.role: Accessible.RadioButton
    Accessible.checked: selected
    Accessible.onPressAction: activated()
    Accessible.onToggleAction: activated()

    Item {
        id: body
        x: card.padding
        y: card.padding
        width: card.width - card.padding * 2
        height: card.height - card.padding * 2
    }
    // The check pops in on the chosen card and shrinks away.
    Rectangle {
        width: 18
        height: 18
        radius: width / 2
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        color: Theme.accent
        scale: card.selected ? 1 : Theme.motionScale(0.2)
        opacity: card.selected ? 1 : 0
        Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
        Behavior on opacity { Ease { duration: Theme.fast } }
        Icon { anchors.centerIn: parent; name: "check"; size: 12; color: Theme.accentText }
    }
    FocusRing { target: card; shown: card.focused }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; onTapped: card.activated() }
}
