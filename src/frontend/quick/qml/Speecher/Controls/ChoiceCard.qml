import QtQuick
import Speecher.Theme

// A card that can be chosen as a whole, the shell of OptionCards: the chosen
// one takes a pencil wash and outline, the others lighten under the pointer,
// and every card gives a little when pressed. Only whole-card choices press
// in; a card that opens is a Card with a clickable header instead. checked adds the
// CheckMark in the top right corner; put it elsewhere yourself with
// checked: false. Content goes inside, laid out within padding.
// interactive: false leaves the pointer to the content, for a card where only
// a part of it chooses; that part then sets hovered and pressed.
Rectangle {
    id: card

    property bool selected: false
    property bool checked: true
    property bool focused: false
    property bool interactive: true
    property bool hovered: hover.hovered
    property bool pressed: tap.pressed
    property int padding: 14
    default property alias content: body.data
    signal activated()

    implicitHeight: body.childrenRect.height + padding * 2
    radius: Theme.radiusLarge
    color: selected ? Theme.accentSoft : card.hovered ? Theme.fill : "transparent"
    border.width: 1
    border.color: selected ? Theme.accent : card.hovered ? Theme.borderStrong : Theme.border
    opacity: enabled ? 1 : Theme.disabledOpacity
    scale: card.pressed ? Theme.motionScale(0.98) : 1
    Behavior on color { EaseColor {} }
    Behavior on border.color { EaseColor {} }
    Behavior on opacity { Ease {} }
    Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }
    Accessible.role: Accessible.RadioButton
    Accessible.checked: selected
    Accessible.onPressAction: activated()

    Item {
        id: body
        x: card.padding
        y: card.padding
        width: card.width - card.padding * 2
        height: card.height - card.padding * 2
    }
    CheckMark {
        visible: card.checked
        shown: card.selected
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
    }
    FocusRing { target: card; shown: card.focused }

    HoverHandler { id: hover; enabled: card.interactive; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; enabled: card.interactive; onTapped: card.activated() }
}
