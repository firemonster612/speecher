import QtQuick
import Speecher.Theme

// One stop in a run of steps: a numbered node, its title beside it and an
// optional line of detail under the title. stage: todo, current, done or
// warning (done, but its check did not pass). The node's number, tick and
// warning mark cross-fade as the state moves on, and the current node grows
// a little. Wrappers lay nodes out in a row or down a rail and draw the
// lines between them; set interactive to make a node a button, which takes
// keyboard focus and answers Return and Space.
Item {
    id: node

    property int number: 1
    property string title
    property string detail
    property string stage: "todo"
    property bool interactive: false
    property int size: 24
    signal activated()

    readonly property bool _done: stage === "done"
    readonly property bool _warn: stage === "warning"
    readonly property bool _current: stage === "current"

    implicitWidth: size + (title.length ? Theme.space3 + Math.max(titleText.implicitWidth, detailText.implicitWidth) : 0)
    implicitHeight: Math.max(size, words.y + words.implicitHeight)
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    activeFocusOnTab: interactive
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: title
    Accessible.onPressAction: if (interactive) activated()
    Keys.onReturnPressed: { focusVisible.keyed(); if (interactive) activated() }
    Keys.onSpacePressed: { focusVisible.keyed(); if (interactive) activated() }
    FocusVisible { id: focusVisible }

    // A node you can pick takes the rows' faint fill under the pointer.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.space1
        radius: Theme.radius
        color: !node.interactive ? "transparent" : tap.pressed ? Theme.fillStrong : hover.hovered ? Theme.fill : "transparent"
        Behavior on color { EaseColor {} }
    }

    // Keeps a line behind the node from showing through a translucent fill.
    Rectangle { width: node.size; height: node.size; radius: width / 2; color: Theme.paper; scale: disc.scale }
    Rectangle {
        id: disc
        width: node.size
        height: node.size
        radius: width / 2
        color: node._warn ? Theme.soft(Theme.warning, 0.2) : node._done ? Theme.accent : node._current ? Theme.sheet : Theme.paper
        border.width: node._done ? 0 : 1.5
        border.color: node._warn ? Theme.warning : node._current ? Theme.accent : Theme.borderStrong
        scale: node._current ? Theme.motionScale(1.08) : 1
        Behavior on color { EaseColor { duration: Theme.base } }
        Behavior on border.color { EaseColor { duration: Theme.base } }
        Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }

        Txt {
            anchors.centerIn: parent
            text: node._warn ? "!" : node.number
            variant: "caption"
            strong: true
            color: node._warn ? Theme.warning : node._current ? Theme.accent : Theme.textTertiary
            opacity: node._done ? 0 : 1
            Behavior on opacity { Ease { duration: Theme.fast } }
        }
        Icon {
            anchors.centerIn: parent
            name: "check"
            size: Math.round(node.size * 0.54)
            color: Theme.accentText
            opacity: node._done ? 1 : 0
            scale: node._done ? 1 : Theme.motionScale(0.3)
            Behavior on opacity { Ease { duration: Theme.fast } }
            Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
        }
    }

    Column {
        id: words
        visible: node.title.length > 0
        x: node.size + Theme.space3
        y: Math.max(0, (node.size - titleText.implicitHeight) / 2)
        width: node.width - x
        spacing: 1
        Txt {
            id: titleText
            text: node.title
            variant: "label"
            strong: node._current
            tone: node._current ? "primary" : node._done || node._warn ? "secondary" : "tertiary"
            width: parent.width
            elide: Text.ElideRight
        }
        Reveal {
            open: node.detail.length > 0
            Txt {
                id: detailText
                text: node.detail
                variant: "caption"
                tone: node._warn ? "warning" : "tertiary"
                width: parent.width
                elide: Text.ElideRight
            }
        }
    }

    FocusRing { target: disc; shown: focusVisible.shown; targetRadius: node.size / 2 }

    HoverHandler { id: hover; enabled: node.interactive; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; enabled: node.interactive; onTapped: { focusVisible.pointed(); node.activated() } }
}
