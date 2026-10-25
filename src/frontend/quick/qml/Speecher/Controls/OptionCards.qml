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
    activeFocusOnTab: true
    Accessible.role: Accessible.List
    Keys.onLeftPressed: { focusVisible.keyed(); _choose(Math.max(0, currentIndex - 1)) }
    Keys.onRightPressed: { focusVisible.keyed(); _choose(Math.min(options.length - 1, currentIndex + 1)) }
    function _choose(i) { activated(options[i].id) }
    FocusVisible { id: focusVisible }

    Row {
        id: row
        width: parent.width
        spacing: 10
        Repeater {
            model: root.options
            delegate: ChoiceCard {
                id: card
                required property var modelData
                required property int index
                Accessible.name: modelData.label

                width: (row.width - row.spacing * (root.options.length - 1)) / root.options.length
                height: Math.max(100, implicitHeight)
                selected: index === root.currentIndex
                onActivated: { focusVisible.pointed(); root.forceActiveFocus(); root._choose(index) }
                focused: focusVisible.shown && selected

                Column {
                    width: parent.width
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
            }
        }
    }
}
