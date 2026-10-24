pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// A row of mutually exclusive choices with a thumb that slides to the
// chosen one. options: [{ id, label, icon? }]. It shows currentId and only
// asks for a change through activated: the caller sets currentId.
FocusScope {
    id: root

    property var options: []
    property string currentId: options.length ? options[0].id : ""
    property bool stretch: false
    signal activated(string id)

    readonly property int currentIndex: Math.max(0, options.findIndex(o => o.id === currentId))

    implicitHeight: 30
    implicitWidth: row.implicitWidth + 6
    activeFocusOnTab: true
    Accessible.role: Accessible.PageTabList

    Keys.onLeftPressed: { focusVisible.keyed(); _choose(Math.max(0, currentIndex - 1)) }
    Keys.onRightPressed: { focusVisible.keyed(); _choose(Math.min(options.length - 1, currentIndex + 1)) }
    function _choose(i) { activated(options[i].id) }
    FocusVisible { id: focusVisible }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.fill
        border.width: 1
        border.color: Theme.rule
    }

    Rectangle {
        id: thumb
        readonly property Item target: repeater.count > root.currentIndex ? repeater.itemAt(root.currentIndex) : null
        x: target ? row.x + target.x : 3
        width: target ? target.width : 0
        y: 3
        height: root.height - 6
        radius: Theme.radiusSmall
        color: Theme.raised
        border.width: 1
        border.color: Theme.border
        // Under reduced motion the thumb moves at once.
        Behavior on x { enabled: Theme.movement > 0; Ease { duration: Theme.slow; curve: Theme.springy } }
        Behavior on width { enabled: Theme.movement > 0; Ease { duration: Theme.slow } }
    }
    FocusRing { target: track; shown: focusVisible.shown }

    Row {
        id: row
        x: 3
        height: parent.height
        Repeater {
            id: repeater
            model: root.options
            delegate: Item {
                id: seg
                required property var modelData
                required property int index
                readonly property bool selected: index === root.currentIndex
                width: root.stretch ? (root.width - 6) / root.options.length : content.implicitWidth + 24
                height: row.height
                Accessible.role: Accessible.PageTab
                Accessible.name: seg.modelData.label
                Accessible.selected: seg.selected

                // Hovering an option other than the chosen one shows a faint
                // pill where the thumb would land.
                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 3
                    anchors.bottomMargin: 3
                    radius: Theme.radiusSmall
                    color: Theme.fillStrong
                    opacity: hover.hovered && !seg.selected ? 1 : 0
                    Behavior on opacity { Ease { duration: Theme.fast } }
                }

                Row {
                    id: content
                    anchors.centerIn: parent
                    spacing: 6
                    Icon {
                        visible: !!seg.modelData.icon
                        name: seg.modelData.icon || ""
                        size: 14
                        color: seg.selected ? Theme.text : Theme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Txt {
                        text: seg.modelData.label
                        variant: "labelSmall"
                        tone: seg.selected ? "primary" : "secondary"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                HoverHandler { id: hover }
                TapHandler { onTapped: { focusVisible.pointed(); root.forceActiveFocus(); root._choose(seg.index) } }
            }
        }
    }
}
