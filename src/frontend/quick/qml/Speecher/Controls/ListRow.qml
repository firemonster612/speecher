import QtQuick
import Speecher.Theme

// A row in a list on a card, for data rather than settings (use SettingRow
// for those): an optional icon, a title over a quieter line of detail, laid
// out as SettingRow lays out its title and help; an inset divider above
// every row but the first; a faint fill under the pointer. At the right
// end, trailing holds what goes with the row and always shows (a value, a
// status, a switch), and actions holds buttons that show while the row is
// hovered or one of them has keyboard focus. Rows that need more than a
// title and detail put their own content in the default slot, which fills
// the inset left of the right end. In a RowList, set first from the
// delegate's index and the list's first. A clickable row is a button: it
// takes keyboard focus and answers Return and Space.
Item {
    id: row

    property string title
    property string detail
    property string iconName
    property string titleVariant: "label"
    // A long title, such as a dictation, wraps instead of eliding.
    property bool wrapTitle: false
    property int detailLines: 1
    property bool first: Positioner.isFirstItem
    property bool clickable: false
    property bool selected: false
    readonly property int inset: Theme.rowInset
    readonly property alias hovered: hover.hovered
    default property alias content: body.data
    property alias trailing: trailingRow.data
    property alias actions: actionRow.data
    signal clicked()

    width: parent ? parent.width : implicitWidth
    implicitHeight: title.length ? Math.max(Theme.rowHeight, words.implicitHeight + 22) : Theme.rowHeight
    activeFocusOnTab: clickable
    Accessible.role: clickable ? Accessible.Button : Accessible.ListItem
    Accessible.name: title
    Accessible.description: detail
    Accessible.onPressAction: if (clickable) clicked()
    Keys.onReturnPressed: { focusVisible.keyed(); if (clickable) clicked() }
    Keys.onSpacePressed: { focusVisible.keyed(); if (clickable) clicked() }
    FocusVisible { id: focusVisible }

    Rectangle {
        id: wash
        anchors.fill: parent
        anchors.margins: Theme.washInset
        radius: Theme.washRadius
        color: row.selected ? Theme.accentSoft : !row.clickable ? "transparent"
             : tap.pressed ? Theme.fillStrong : row.hovered ? Theme.fill : "transparent"
        Behavior on color { EaseColor {} }
    }
    FocusRing { target: wash; shown: focusVisible.shown }
    Rectangle {
        visible: !row.first
        x: row.inset
        width: parent.width - row.inset * 2
        height: 1
        color: Theme.rule
    }
    Item {
        id: body
        anchors.fill: parent
        anchors.leftMargin: row.inset
        anchors.rightMargin: ends.width > 0 ? row.width - ends.x + Theme.space2 : row.inset
        // The trailing controls fade themselves when the row is disabled.
        opacity: row.enabled ? 1 : Theme.disabledOpacity
        Behavior on opacity { Ease {} }

        Icon {
            visible: row.iconName.length > 0
            y: words.y + 1
            name: row.iconName
            size: 16
            color: Theme.textSecondary
        }
        Column {
            id: words
            visible: row.title.length > 0
            x: row.iconName.length ? 28 : 0
            width: parent.width - x
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Txt {
                width: parent.width
                text: row.title
                variant: row.titleVariant
                wrapMode: row.wrapTitle ? Text.WordWrap : Text.NoWrap
                elide: row.wrapTitle ? Text.ElideNone : Text.ElideMiddle
            }
            Txt {
                visible: text.length > 0
                width: parent.width
                text: row.detail
                variant: "small"
                tone: "secondary"
                wrapMode: row.detailLines > 1 ? Text.WordWrap : Text.NoWrap
                // Set only when wrapping: a line limit on one line drops the
                // line's leading and the row comes out shorter than its peers.
                maximumLineCount: row.detailLines > 1 ? row.detailLines : 2147483647
                elide: Text.ElideRight
            }
        }
    }
    Row {
        id: ends
        anchors.right: parent.right
        anchors.rightMargin: row.inset - (actionRow.width > 0 ? 4 : 0)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.space2
        Row { id: trailingRow; spacing: Theme.space2; anchors.verticalCenter: parent.verticalCenter }
        FocusScope {
            width: actionRow.width
            height: actionRow.height
            anchors.verticalCenter: parent.verticalCenter
            opacity: row.hovered || activeFocus ? 1 : 0
            Behavior on opacity { Ease { duration: Theme.fast } }
            Row { id: actionRow; spacing: 2 }
        }
    }

    HoverHandler { id: hover; cursorShape: row.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { id: tap; enabled: row.clickable; onTapped: { focusVisible.pointed(); row.clicked() } }
}
