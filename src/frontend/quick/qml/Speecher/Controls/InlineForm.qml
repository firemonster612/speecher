import QtQuick
import Speecher.Theme

// An add or edit form that opens in place, usually at the top of a card's
// list, instead of a dialog. The caller opens it, decides what saving does
// and closes it on saved or cancelled. A faint fill sets it apart from the
// rows around it. Fields go in the default slot, one per line; put two in a
// Row to set them side by side.
Reveal {
    id: root

    property string title
    property string saveText
    property string cancelText
    property bool canSave: true
    default property alias fields: fieldColumn.data
    signal saved()
    signal cancelled()

    Item {
        width: parent.width
        implicitHeight: form.implicitHeight + Theme.space5 * 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: Theme.radius
            color: Theme.fill
        }
        Column {
            id: form
            x: Theme.rowInset
            y: Theme.space5
            width: parent.width - Theme.rowInset * 2
            spacing: Theme.space4

            Txt { visible: text.length > 0; text: root.title; variant: "heading" }
            Column {
                id: fieldColumn
                width: parent.width
                spacing: 14
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.space2
                Button { text: root.cancelText; variant: "ghost"; onClicked: root.cancelled() }
                Button { text: root.saveText; variant: "primary"; enabled: root.canSave; onClicked: root.saved() }
            }
        }
    }
}
