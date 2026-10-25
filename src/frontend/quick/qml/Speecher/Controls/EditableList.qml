pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// Things a person made, such as their own tones, as rows on a card: the add
// or edit form (the default slot, usually an InlineForm), the rows with
// edit and delete on hover, and a line saying what to add when there are
// none. items: [{ id?, title, detail }], keyed by id or else title.
Column {
    id: root

    property var items: []
    property string emptyText
    property bool formOpen: false
    property string editText
    property string removeText
    default property alias form: formSlot.data
    signal editClicked(int index)
    signal removeClicked(int index)

    width: parent ? parent.width : implicitWidth

    Column { id: formSlot; width: parent.width }

    RowList {
        id: list
        values: root.items
        keyOf: item => String(item.id !== undefined ? item.id : item.title)
        delegate: ListRow {
            id: row
            required property string key
            required property int index
            readonly property var item: list.entry(key)
            width: ListView.view.width
            first: index === 0 && list.first
            title: item.title
            detail: item.detail || ""
            detailLines: 2
            actions: [
                IconButton { iconName: "pencil"; size: 28; tooltip: root.editText; onClicked: root.editClicked(row.index) },
                IconButton { iconName: "trash-2"; size: 28; tooltip: root.removeText; onClicked: root.removeClicked(row.index) }
            ]
        }
    }

    Reveal {
        open: root.items.length === 0 && !root.formOpen
        Txt {
            x: Theme.rowInset
            width: parent.width - Theme.rowInset * 2
            topPadding: 18
            bottomPadding: 18
            text: root.emptyText
            tone: "secondary"
            wrapMode: Text.WordWrap
        }
    }
}
