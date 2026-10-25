import QtQuick

// A row that opens to show more rows under it, for settings few people
// need. Clicking the row (or Space on it) turns the chevron and slides the
// content open, and emits toggled. highlight() flashes the row, for a
// search that lands on it.
Column {
    id: root

    property string title
    property string help
    property string iconName
    property string trailingText
    property bool open: false
    default property alias content: reveal.content
    signal toggled(bool open)

    function toggle() { open = !open; toggled(open) }
    function highlight() { head.highlight() }

    width: parent ? parent.width : implicitWidth

    SettingRow {
        id: head
        first: root.Positioner.isFirstItem
        title: root.title
        help: root.help
        iconName: root.iconName
        clickable: true
        trailingText: root.trailingText
        trailingIcon: "chevron-down"
        trailingRotation: root.open ? 180 : 0
        Accessible.role: Accessible.Button
        onClicked: root.toggle()
    }

    Reveal {
        id: reveal
        open: root.open
    }
}
