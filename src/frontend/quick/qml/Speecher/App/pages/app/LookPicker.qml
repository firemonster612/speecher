pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls

// Looks to choose from as small pictures over their names, in rows of
// columns: the theme, the colours, the layout. One pencil outline glides to
// the chosen one, and fades when the choice is in another picker; arrows
// move it. thumbnail draws a picture: its root takes the option as option.
FocusScope {
    id: root

    property var options: []
    property string currentId
    property int columns: Math.max(1, options.length)
    property Component thumbnail
    signal activated(string id)

    readonly property int currentIndex: options.findIndex(o => o.id === currentId)
    readonly property int _gap: Theme.space3
    readonly property real _cellWidth: (width - _gap * (columns - 1)) / columns
    readonly property int _thumbHeight: Math.round(_cellWidth * 0.6)
    // A picture, the name under it, and room for the outline round it.
    readonly property int _cellHeight: _thumbHeight + 32
    readonly property int _rows: Math.ceil(options.length / columns)

    implicitWidth: 420
    implicitHeight: _rows * _cellHeight + (_rows - 1) * _gap
    opacity: enabled ? 1 : Theme.disabledOpacity
    Behavior on opacity { Ease {} }
    activeFocusOnTab: true
    Keys.onLeftPressed: _step(-1)
    Keys.onRightPressed: _step(1)
    Keys.onUpPressed: _step(-columns)
    Keys.onDownPressed: _step(columns)
    function _step(by) {
        focusVisible.keyed()
        _choose(Math.max(0, Math.min(options.length - 1, currentIndex < 0 ? 0 : currentIndex + by)))
    }
    function _choose(i) { activated(options[i].id) }
    function _x(i) { return (i % columns) * (_cellWidth + _gap) }
    function _y(i) { return Math.floor(i / columns) * (_cellHeight + _gap) }
    FocusVisible { id: focusVisible }

    Rectangle {
        id: ring
        readonly property int at: Math.max(0, root.currentIndex)
        x: root._x(at) - 4
        y: root._y(at)
        width: root._cellWidth + 8
        height: root._thumbHeight + 8
        radius: Theme.radiusPanel
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: root.currentIndex < 0 ? 0 : 1
        Behavior on opacity { Ease {} }
        Behavior on x { enabled: Theme.movement > 0; Ease { duration: Theme.slow; curve: Theme.springy } }
        Behavior on y { enabled: Theme.movement > 0; Ease { duration: Theme.slow; curve: Theme.springy } }
    }
    FocusRing { target: ring; shown: focusVisible.shown }

    Repeater {
        model: root.options
        delegate: Item {
            id: option
            required property var modelData
            required property int index
            readonly property bool selected: index === root.currentIndex

            x: root._x(index)
            y: root._y(index) + 4
            width: root._cellWidth
            height: root._cellHeight - 4

            Item {
                width: parent.width
                height: root._thumbHeight
                // Pressed like a choice card; nothing grows under the pointer.
                scale: tap.pressed ? Theme.motionScale(0.98) : 1
                Behavior on scale { Ease { duration: Theme.fast; curve: Theme.springy } }

                Loader {
                    id: picture
                    anchors.fill: parent
                    sourceComponent: root.thumbnail
                }
                // By name: the thumbnail's own type declares option.
                Binding { target: picture.item; property: "option"; value: option.modelData; when: picture.item !== null }
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusLarge
                    color: "transparent"
                    border.width: 1
                    border.color: hover.hovered ? Theme.borderStrong : Theme.border
                    Behavior on border.color { EaseColor {} }
                }
            }

            Txt {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: Math.min(implicitWidth, parent.width)
                elide: Text.ElideRight
                text: option.modelData.label
                variant: "label"
                tone: option.selected ? "primary" : "secondary"
            }

            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: tap; onTapped: { focusVisible.pointed(); root.forceActiveFocus(); root._choose(option.index) } }
        }
    }
}
