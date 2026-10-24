import QtQuick

// A labelled field in a form: the label, the control, then a line of help.
Column {
    id: root

    property string label
    property string help
    default property alias control: slot.data

    width: parent ? parent.width : implicitWidth
    spacing: 6

    Txt { text: root.label; variant: "label"; visible: text.length > 0 }
    Item {
        id: slot
        width: parent.width
        height: childrenRect.height
    }
    Txt {
        visible: root.help.length > 0
        text: root.help
        variant: "small"
        tone: "secondary"
        width: parent.width
        wrapMode: Text.WordWrap
    }
}
