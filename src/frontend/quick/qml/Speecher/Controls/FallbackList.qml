pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// An ordered list of fallbacks: drag a row by its grip or use its arrows to
// reorder, remove one, or add one of the options left. Rows slide out of
// each other's way as the order changes, and fold in and out as they come
// and go. The page owns the data and adapts it:
//   options: every option that may be listed, [{ id, label, icon?, status?, tone? }]
//            (a tone of negative or warning marks a row that can't run)
//   statusOf: optional (id, index) => ({ text, tone }), for a status that
//            depends on the row's place in the order
// emptyText and addLabel give the list its own empty and add rows; leave
// them empty to put those elsewhere. edited fires with the new order.
Column {
    id: root

    property var ids: []
    property var options: []
    property var statusOf: null
    property int maxCount: 99
    property int rowHeight: 60
    property string emptyText
    property string addLabel
    property string addHelp
    property string addPlaceholder
    property string moveUpText
    property string moveDownText
    property string removeText
    signal edited(var ids)

    readonly property var available: options.filter(o => ids.indexOf(o.id) < 0)
    // Where the row being dragged sits now.
    property int _dragIndex: -1

    function _option(id) { return options.find(o => o.id === id) || ({ id: id, label: id }) }
    function _status(id, index) {
        if (statusOf) return statusOf(id, index)
        const o = _option(id)
        return { text: o.status || "", tone: o.tone || "neutral" }
    }
    function _commit() { edited(list.keys()) }
    function _move(from, to) { list.moveRow(from, to); _commit() }

    width: parent ? parent.width : implicitWidth

    SettingRow {
        shown: root.emptyText.length > 0 && root.ids.length === 0
        title: root.emptyText
    }

    RowList {
        id: list
        values: root.ids

        delegate: Item {
            id: slot
            required property int index
            required property string key
            readonly property var option: root._option(key)
            readonly property var status: root._status(key, index)

            width: ListView.view.width
            height: root.rowHeight
            onIndexChanged: if (grip.active) root._dragIndex = index

            // Another row dragged over this one takes its place.
            DropArea {
                anchors.fill: parent
                keys: ["fallback"]
                onEntered: if (root._dragIndex >= 0 && root._dragIndex !== slot.index) list.moveRow(root._dragIndex, slot.index)
            }

            ListRow {
                id: row
                width: slot.width
                height: slot.height
                anchors.verticalCenter: parent.verticalCenter
                first: slot.index === 0 && list.first || grip.active
                Drag.active: grip.active
                Drag.source: row
                Drag.keys: ["fallback"]
                Drag.hotSpot.y: height / 2

                states: State {
                    when: grip.active
                    ParentChange { target: row; parent: list }
                    AnchorChanges { target: row; anchors.verticalCenter: undefined }
                    PropertyChanges { row.z: 10; row.scale: Theme.motionScale(1.02); lift.opacity: 1 }
                }
                transitions: Transition {
                    from: "*"; to: ""
                    ParentAnimation { AnchorAnimation { duration: Theme.base; easing.type: Easing.BezierSpline; easing.bezierCurve: Theme.easeOut } }
                    Ease { property: "scale"; duration: Theme.base; curve: Theme.springy }
                }

                // The card it lifts onto while dragged.
                Rectangle {
                    id: lift
                    anchors.fill: parent
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    anchors.leftMargin: -row.inset + 3
                    anchors.rightMargin: -row.inset + 3
                    radius: Theme.radius
                    color: Theme.raised
                    border.width: 1
                    border.color: Theme.borderStrong
                    opacity: 0
                    Behavior on opacity { Ease { duration: Theme.fast } }
                }

                Item {
                    id: handle
                    x: -4
                    width: 24
                    height: parent.height
                    Icon {
                        anchors.centerIn: parent
                        name: "grip-vertical"
                        size: 16
                        color: grip.active || row.hovered ? Theme.textSecondary : Theme.textTertiary
                    }
                    HoverHandler { cursorShape: grip.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor }
                    DragHandler {
                        id: grip
                        target: row
                        xAxis.enabled: false
                        onActiveChanged: {
                            root._dragIndex = active ? slot.index : -1
                            if (!active) root._commit()
                        }
                    }
                }
                Txt {
                    x: 26
                    width: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: slot.index + 1
                    variant: "mono"
                    tone: "tertiary"
                }
                Icon {
                    id: glyph
                    visible: !!slot.option.icon
                    x: 46
                    anchors.verticalCenter: parent.verticalCenter
                    name: slot.option.icon || ""
                    size: 16
                    color: Theme.textSecondary
                }
                Column {
                    x: glyph.visible ? 74 : 48
                    width: parent.width - x
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Txt { text: slot.option.label; variant: "label"; width: parent.width; elide: Text.ElideRight }
                    StatusText {
                        visible: slot.status.text.length > 0
                        text: slot.status.text
                        tone: slot.status.tone
                    }
                }

                trailing: [
                    IconButton {
                        iconName: "arrow-up"
                        tooltip: root.moveUpText
                        enabled: slot.index > 0
                        onClicked: root._move(slot.index, slot.index - 1)
                    },
                    IconButton {
                        iconName: "arrow-down"
                        tooltip: root.moveDownText
                        enabled: slot.index < list.count - 1
                        onClicked: root._move(slot.index, slot.index + 1)
                    },
                    IconButton {
                        iconName: "x"
                        tooltip: root.removeText
                        onClicked: root.edited(root.ids.filter(id => id !== slot.key))
                    }
                ]
            }
        }
    }

    // One more, from the options not in the list yet.
    SettingRow {
        shown: root.addLabel.length > 0 && root.ids.length < root.maxCount && root.available.length > 0
        title: root.addLabel
        help: root.addHelp
        iconName: "plus"
        Select {
            options: root.available
            currentId: ""
            placeholder: root.addPlaceholder
            onChosen: id => root.edited(root.ids.concat([id]))
        }
    }
}
