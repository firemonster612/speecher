import QtQuick
import Speecher.Theme

// The question that opens in place before an action that can't be undone,
// instead of a dialog: the question and what it costs on the left, with
// leading content such as a check box under them, and Cancel and the action
// on the right. Put it under the row
// it belongs to, or in place of a footer it replaces. The caller does the
// action on confirmed and closes it on either signal.
Reveal {
    id: root

    property string text
    property string detail
    property string confirmText
    property string confirmIcon: "trash-2"
    property string cancelText
    // danger for deleting and removing; primary for anything else.
    property string variant: "danger"
    property int inset: Theme.rowInset
    property bool ruled: true
    property alias leading: leadingSlot.data
    signal confirmed()
    signal cancelled()

    Item {
        width: parent.width
        implicitHeight: Math.max(Theme.rowHeight, question.implicitHeight + Theme.space4 * 2)

        Rectangle { visible: root.ruled; x: root.inset; width: parent.width - root.inset * 2; height: 1; color: Theme.rule }
        Column {
            id: question
            x: root.inset
            width: buttons.x - x - Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Txt { text: root.text; variant: "label"; width: parent.width; wrapMode: Text.WordWrap }
            Txt { visible: text.length > 0; text: root.detail; variant: "small"; tone: "secondary"; width: parent.width; wrapMode: Text.WordWrap }
            Column { id: leadingSlot; visible: children.length > 0; topPadding: Theme.space2 }
        }
        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: root.inset
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Button { text: root.cancelText; variant: "ghost"; size: "small"; onClicked: root.cancelled() }
            Button { text: root.confirmText; variant: root.variant; iconName: root.confirmIcon; size: "small"; onClicked: root.confirmed() }
        }
    }
}
