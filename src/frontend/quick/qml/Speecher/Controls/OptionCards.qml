pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// A choice laid out as cards side by side, each with an icon, a title and a
// line of help. The chosen card gets the pencil outline and a check. Left
// and right arrows move the choice. It shows currentId and only asks for a
// change through activated: the caller sets currentId.
FocusScope {
    id: root

    property var options: []
    property string currentId: options.length ? options[0].id : ""
    signal activated(string id)

    readonly property int currentIndex: Math.max(0, options.findIndex(o => o.id === currentId))

    implicitWidth: 520
    implicitHeight: row.implicitHeight
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    activeFocusOnTab: true
    Accessible.role: Accessible.List
    Keys.onLeftPressed: { focusVisible.keyed(); _choose(Math.max(0, currentIndex - 1)) }
    Keys.onRightPressed: { focusVisible.keyed(); _choose(Math.min(options.length - 1, currentIndex + 1)) }
    function _choose(i) { activated(options[i].id) }
    FocusVisible { id: focusVisible }

    Row {
        id: row
        width: parent.width
        spacing: Theme.space3
        Repeater {
            model: root.options
            // The chosen card takes a pencil wash, outline and check; the
            // others lighten under the pointer, and every card gives a
            // little when pressed, as a whole-card choice does.
            delegate: Rectangle {
                id: card
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex
                readonly property int padding: 14
                function choose() { focusVisible.pointed(); root.forceActiveFocus(); root._choose(index) }

                width: (row.width - row.spacing * (root.options.length - 1)) / root.options.length
                height: Math.max(100, body.implicitHeight + padding * 2)
                radius: Theme.radiusLarge
                color: selected ? Theme.accentSoft : hover.hovered ? Theme.fill : "transparent"
                border.width: 1
                border.color: selected ? Theme.accent : hover.hovered ? Theme.borderStrong : Theme.border
                scale: tap.pressed ? Theme.motionScale(0.98) : 1
                Behavior on color { EaseColor {} }
                Behavior on border.color { EaseColor {} }
                Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: selected
                Accessible.onPressAction: choose()
                Accessible.onToggleAction: choose()

                Column {
                    id: body
                    x: card.padding
                    y: card.padding
                    width: card.width - card.padding * 2
                    spacing: 10
                    Icon {
                        name: card.modelData.icon || "circle"
                        size: 18
                        color: card.selected ? Theme.accent : Theme.textSecondary
                    }
                    Column {
                        width: parent.width
                        spacing: 3
                        Txt { text: card.modelData.label; variant: "label" }
                        Txt { text: card.modelData.help || ""; variant: "small"; tone: "secondary"; width: parent.width; wrapMode: Text.WordWrap }
                    }
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
                FocusRing { target: card; shown: focusVisible.shown && card.selected }

                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { id: tap; onTapped: card.choose() }
            }
        }
    }
}
