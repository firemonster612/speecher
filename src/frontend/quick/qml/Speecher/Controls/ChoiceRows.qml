pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// A choice as rows on a card, one option each with a line of help and its
// status on the right. A pencil wash slides to the chosen row, and options
// that come and go fold in and out. Up and down arrows move the choice.
//   options: [{ id, label, help?, icon?, recommended?, status?, tone? }]
//            tone colours the status (warning, negative, positive); the
//            page decides which options need it.
//   indicator: radio (a radio before each option) or check (the option's
//            icon before it and a check after the chosen one).
// ruleBelow draws an inset divider under the last row, for a row that opens
// after it in the same card.
FocusScope {
    id: root

    property var options: []
    property string currentId
    property string indicator: "radio"
    property int rowHeight: 58
    property bool ruleBelow: false
    property string recommendedText
    signal activated(string id)

    readonly property int currentIndex: options.findIndex(o => o.id === currentId)

    implicitWidth: 520
    implicitHeight: list.implicitHeight
    width: parent ? parent.width : implicitWidth
    activeFocusOnTab: true
    Accessible.role: Accessible.List
    Keys.onUpPressed: { focusVisible.keyed(); _choose(Math.max(0, currentIndex - 1)) }
    Keys.onDownPressed: { focusVisible.keyed(); _choose(Math.min(options.length - 1, currentIndex + 1)) }
    function _choose(i) { activated(options[i].id) }
    FocusVisible { id: focusVisible }

    Rectangle {
        id: selection
        x: 3
        y: Math.max(0, root.currentIndex) * root.rowHeight + 3
        width: parent.width - 6
        height: root.rowHeight - 6
        radius: Theme.radius
        color: Theme.accentSoft
        opacity: root.currentIndex >= 0 ? 1 : 0
        // Under reduced motion the selection moves at once.
        Behavior on y { enabled: Theme.movement > 0; Ease { duration: Theme.slow; curve: Theme.springy } }
        Behavior on opacity { Ease {} }
    }
    FocusRing { target: selection; shown: focusVisible.shown }
    Rectangle {
        x: Theme.rowInset
        y: parent.height - 1
        width: parent.width - Theme.rowInset * 2
        height: 1
        color: Theme.rule
        opacity: root.ruleBelow ? 1 : 0
        Behavior on opacity { Ease {} }
    }

    RowList {
        id: list
        values: root.options
        delegate: Item {
            id: option
            required property string key
            required property int index
            readonly property var entry: list.entry(key)
            readonly property bool selected: index === root.currentIndex

            width: root.width
            height: root.rowHeight
            Accessible.role: Accessible.RadioButton
            Accessible.name: entry.label
            Accessible.checked: selected

            Rectangle {
                anchors.fill: parent
                anchors.margins: 3
                radius: Theme.radius
                color: hover.hovered && !option.selected ? Theme.fill : "transparent"
                Behavior on color { EaseColor {} }
            }
            // The wash covers the lines next to the chosen row.
            Rectangle {
                visible: option.index > 0
                x: Theme.rowInset
                width: parent.width - Theme.rowInset * 2
                height: 1
                color: Theme.rule
                opacity: option.selected || option.index - 1 === root.currentIndex ? 0 : 1
                Behavior on opacity { Ease { duration: Theme.fast } }
            }

            Rectangle {
                id: radio
                visible: root.indicator === "radio"
                x: Theme.rowInset
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: "transparent"
                border.width: option.selected ? 5.5 : 1.5
                border.color: option.selected ? Theme.accent : Theme.borderStrong
                Behavior on border.width { Ease { duration: Theme.slow; curve: Theme.springy } }
                Behavior on border.color { EaseColor { duration: Theme.base } }
            }
            Icon {
                id: glyph
                visible: root.indicator === "check" && !!option.entry.icon
                x: Theme.rowInset
                anchors.verticalCenter: parent.verticalCenter
                name: option.entry.icon || ""
                size: 16
                color: option.selected ? Theme.accent : Theme.textSecondary
            }
            Column {
                x: radio.visible || glyph.visible ? Theme.rowInset + 30 : Theme.rowInset
                width: trail.x - x - Theme.space4
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Row {
                    spacing: 8
                    Txt { id: label; text: option.entry.label; variant: "label" }
                    Txt {
                        visible: !!option.entry.recommended
                        text: root.recommendedText
                        variant: "small"
                        tone: "accent"
                        anchors.baseline: label.baseline
                    }
                }
                Txt {
                    visible: text.length > 0
                    text: option.entry.help || ""
                    variant: "small"
                    tone: "secondary"
                    width: parent.width
                    elide: Text.ElideRight
                }
            }
            Row {
                id: trail
                anchors.right: parent.right
                anchors.rightMargin: Theme.rowInset
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                StatusText {
                    text: option.entry.status || ""
                    tone: option.entry.tone || "neutral"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Icon {
                    visible: root.indicator === "check"
                    name: "check"
                    size: 16
                    color: Theme.accent
                    opacity: option.selected ? 1 : 0
                    scale: option.selected ? 1 : Theme.motionScale(0.4)
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on opacity { Ease { duration: Theme.fast } }
                    Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
                }
            }

            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { focusVisible.pointed(); root.forceActiveFocus(); root._choose(option.index) } }
        }
    }
}
