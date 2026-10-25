pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// The theme as three small windows to choose from: System (half light, half
// dark), Light and Dark, each a sheet with a raised card on paper. One
// pencil outline slides to the chosen one.
FocusScope {
    id: root

    property var options: []
    property string currentId: "system"
    signal activated(string id)

    function _scheme(dark) { return dark ? Theme.darkScheme : Theme.lightScheme }
    readonly property int currentIndex: Math.max(0, options.findIndex(o => o.id === currentId))
    readonly property int _gap: Theme.space3
    readonly property real _cardWidth: (width - _gap * (options.length - 1)) / Math.max(1, options.length)
    readonly property int _thumbHeight: Math.round(_cardWidth * 0.6)

    implicitWidth: 420
    // The outline reaches 4px past the thumbnails, so they start 4px down.
    implicitHeight: _thumbHeight + 32
    activeFocusOnTab: true
    Keys.onLeftPressed: { focusVisible.keyed(); _choose(Math.max(0, currentIndex - 1)) }
    Keys.onRightPressed: { focusVisible.keyed(); _choose(Math.min(options.length - 1, currentIndex + 1)) }
    function _choose(i) { activated(options[i].id) }
    FocusVisible { id: focusVisible }

    Rectangle {
        id: ring
        x: root.currentIndex * (root._cardWidth + root._gap) - 4
        y: 0
        width: root._cardWidth + 8
        height: root._thumbHeight + 8
        radius: Theme.radiusPanel
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        Behavior on x { enabled: Theme.movement > 0; Ease { duration: Theme.slow; curve: Theme.springy } }
    }
    FocusRing { target: ring; shown: focusVisible.shown }

    Repeater {
        model: root.options
        delegate: Item {
            id: option
            required property var modelData
            required property int index
            readonly property bool selected: index === root.currentIndex

            x: index * (root._cardWidth + root._gap)
            y: 4
            width: root._cardWidth
            height: root.height - 4

            Item {
                id: thumb
                width: parent.width
                height: root._thumbHeight
                scale: Theme.motionScale(tap.pressed ? 0.97 : hover.hovered && !option.selected ? 1.02 : 1)
                Behavior on scale { Ease { curve: Theme.springy } }

                // System splits the window down the middle.
                MiniWindow {
                    anchors.fill: parent
                    scheme: root._scheme(option.modelData.id === "dark")
                }
                Item {
                    visible: option.modelData.id === "system"
                    x: parent.width / 2
                    width: parent.width / 2
                    height: parent.height
                    clip: true
                    MiniWindow {
                        x: -parent.x
                        width: thumb.width
                        height: thumb.height
                        scheme: root._scheme(true)
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusLarge
                    color: "transparent"
                    border.width: 1
                    border.color: hover.hovered ? Theme.borderStrong : Theme.border
                }
            }

            Txt {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                text: option.modelData.label
                variant: "label"
                tone: option.selected ? "primary" : "secondary"
            }

            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: tap; onTapped: { focusVisible.pointed(); root.forceActiveFocus(); root._choose(option.index) } }
        }
    }

    // A window in one scheme: paper with a few sidebar entries, and a sheet
    // with a title over a raised card of two rows, one with a pencil switch.
    component MiniWindow: Rectangle {
        id: mini
        property var scheme
        radius: Theme.radiusLarge
        color: mini.scheme.paper
        Repeater {
            model: 4
            Rectangle {
                required property int index
                x: 9; y: 14 + index * 11
                width: index === 1 ? 24 : 20 - index * 2; height: 4; radius: 2
                color: index === 1 ? mini.scheme.accent : Theme.soft(mini.scheme.text, 0.22)
            }
        }
        Rectangle {
            id: sheet
            x: 42; y: 8
            width: parent.width - 50; height: parent.height - 16
            radius: Theme.radius
            color: mini.scheme.sheet
            Rectangle { x: 8; y: 9; width: parent.width * 0.4; height: 5; radius: 2.5; color: mini.scheme.text }
            Rectangle {
                id: miniCard
                x: 8; y: 21
                width: parent.width - 16; height: parent.height - 29
                radius: Theme.radiusSmall
                color: mini.scheme.raised
                border.width: 1
                border.color: mini.scheme.rule
                Repeater {
                    model: 2
                    Rectangle {
                        required property int index
                        x: 6; y: miniCard.height * (index + 0.5) / 2 - 1.5
                        width: miniCard.width * 0.45; height: 3; radius: 1.5
                        color: Theme.soft(mini.scheme.text, 0.35)
                    }
                }
                Rectangle { x: 6; y: miniCard.height / 2; width: miniCard.width - 12; height: 1; color: mini.scheme.rule }
                Rectangle {
                    x: miniCard.width - 20; y: miniCard.height / 4 - 3.5
                    width: 14; height: 7; radius: 3.5
                    color: mini.scheme.accent
                }
            }
        }
    }
}
