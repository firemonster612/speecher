import QtQuick
import Speecher.Theme

// A text field with suggestions under a chevron, for values such as model
// IDs where the list is a guide rather than every choice. Picking one fills
// the field as typing would, so a text bound to a value keeps following
// it; picking or finishing an edit emits committed.
TextField {
    id: field

    property var suggestions: []   // [{ id, label, help?, badge? }]
    property string suggestionsText
    signal committed(string value)

    // As wide as a Select, since it picks from a list too; in a column of
    // settings rows the two line up.
    implicitWidth: Theme.selectWidth

    onEditingFinished: committed(text)
    // The menu belongs to the field, so its close policy leaves a press in
    // the text alone; going back to typing closes it here.
    onPressed: menu.close()

    trailing: IconButton {
        visible: field.suggestions.length > 0
        iconName: "chevron-down"
        size: 24
        tooltip: field.suggestionsText
        rotation: menu.visible ? 180 : 0
        Behavior on rotation { Ease {} }
        onClicked: menu.visible ? menu.close() : menu.open()
    }

    Menu {
        id: menu
        width: Math.max(field.width, 240)
        items: field.suggestions
        currentId: field.text
        onPicked: id => {
            field.remove(0, field.length)
            field.insert(0, id)
            field.committed(id)
        }
    }
}
