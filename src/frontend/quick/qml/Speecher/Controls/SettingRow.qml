import QtQuick
import Speecher.Theme

// One setting: title and help on the left, its control on the right. Set
// stacked to put a wide control under the text instead. helpLink makes a
// part of help a link, which calls helpLinkActivated with the help's text
// to open something from. Inside a card, rows
// after the first draw an inset divider above themselves; set first where
// the row is not a direct child of the card's column.
Item {
    id: row

    property string title
    property string help
    property string helpLink
    property string iconName
    property color iconTint: Theme.textSecondary
    property bool stacked: false
    property bool clickable: false
    property string trailingText
    // What a clickable row shows at its right end: chevron-right for a row
    // that leads somewhere, chevron-down (turned by trailingRotation) for one
    // that opens in place.
    property string trailingIcon: "chevron-right"
    property real trailingRotation: 0
    // Set false to fold the row away (and true to bring it back) with an
    // animation; use this rather than visible, which pops.
    property bool shown: true
    property bool first: Positioner.isFirstItem
    default property alias control: slot.data
    signal clicked()
    signal helpLinkActivated(Item at)

    // A pencil wash that comes and goes once, to show where a search landed.
    function highlight() { flash.restart() }
    property bool _lit: false
    SequentialAnimation {
        id: flash
        PauseAnimation { duration: Theme.ms(380) }
        PropertyAction { target: row; property: "_lit"; value: true }
        PauseAnimation { duration: Theme.ms(900) }
        PropertyAction { target: row; property: "_lit"; value: false }
    }

    // The text, icon and chevron fade together when the row is disabled; the
    // control in the slot fades itself.
    property real _ink: enabled ? 1 : Theme.disabledOpacity
    Behavior on _ink { Ease {} }
    readonly property int _inset: Theme.rowInset
    // Room a clickable row keeps at its right end for its text and chevron.
    readonly property real _trail: clickable ? trail.width + 6 : 0
    readonly property int _textLeft: iconName.length ? _inset + 28 : _inset

    width: parent ? parent.width : implicitWidth
    height: shown ? implicitHeight : 0
    visible: height > 0.5
    opacity: shown ? 1 : 0
    clip: fold.running
    Grow on height { id: fold }
    Behavior on opacity { Ease { duration: row.shown ? Theme.slow : Theme.fast } }
    implicitHeight: stacked ? textCol.implicitHeight + slot.height + 36
                            : Math.max(Theme.rowHeight, Math.max(textCol.implicitHeight, slot.height) + 22)
    Accessible.role: clickable ? Accessible.Button : Accessible.Grouping
    Accessible.name: title
    Accessible.onPressAction: if (clickable) clicked()
    activeFocusOnTab: clickable
    Keys.onSpacePressed: { focusVisible.keyed(); if (clickable) clicked() }
    Keys.onReturnPressed: { focusVisible.keyed(); if (clickable) clicked() }
    FocusVisible { id: focusVisible }

    Rectangle {
        id: wash
        anchors.fill: parent
        anchors.margins: Theme.washInset
        radius: Theme.washRadius
        color: !row.clickable ? "transparent" : tap.pressed ? Theme.fillStrong : hover.hovered ? Theme.fill : "transparent"
        Behavior on color { EaseColor {} }
        FocusRing { target: wash; shown: focusVisible.shown }
    }
    // The search landing's wash lingers as it goes.
    Rectangle {
        anchors.fill: wash
        radius: wash.radius
        color: Theme.accentSoft
        opacity: row._lit ? 1 : 0
        Behavior on opacity { Ease { duration: row._lit ? Theme.fast : Theme.slow * 2 } }
    }
    Rectangle {
        visible: !row.first
        x: row._inset
        width: parent.width - row._inset * 2
        height: 1
        color: Theme.rule
    }

    Icon {
        visible: row.iconName.length > 0
        x: row._inset
        y: textCol.y + 1
        name: row.iconName
        size: 16
        color: row.iconTint
        opacity: row._ink
    }

    Column {
        id: textCol
        x: row._textLeft
        y: row.stacked ? 14 : (row.height - height) / 2
        width: row.stacked ? row.width - x - row._inset : slot.x - x - (slot.width > 0 ? 20 : row._trail)
        spacing: 3
        opacity: row._ink
        Txt { text: row.title; variant: "label"; width: parent.width; wrapMode: Text.WordWrap }
        LinkedText {
            id: helpText
            visible: row.help.length > 0
            plainText: row.help
            link: row.helpLink
            onActivated: row.helpLinkActivated(helpText)
            variant: "small"
            tone: "secondary"
            width: parent.width
            wrapMode: Text.WordWrap
        }
    }

    Item {
        id: slot
        x: row.stacked ? row._textLeft : row.width - width - row._inset - row._trail
        y: row.stacked ? textCol.y + textCol.height + 12 : (row.height - height) / 2
        width: row.stacked ? row.width - row._textLeft - row._inset : childrenRect.width
        height: childrenRect.height
    }

    Row {
        id: trail
        visible: row.clickable
        anchors.right: parent.right
        anchors.rightMargin: row._inset - 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        opacity: row._ink
        Txt { visible: row.trailingText.length > 0; text: row.trailingText; variant: "small"; tone: "secondary"; anchors.verticalCenter: parent.verticalCenter }
        Icon {
            name: row.trailingIcon
            size: 16
            color: hover.hovered ? Theme.textSecondary : Theme.textTertiary
            anchors.verticalCenter: parent.verticalCenter
            rotation: row.trailingRotation
            Behavior on rotation { Ease {} }
            // A chevron that leads somewhere nudges that way.
            transform: Translate {
                x: hover.hovered && row.trailingIcon === "chevron-right" ? 2 * Theme.movement : 0
                Behavior on x { Ease { duration: Theme.fast } }
            }
        }
    }

    HoverHandler { id: hover; enabled: row.clickable; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; enabled: row.clickable; onTapped: { focusVisible.pointed(); row.clicked() } }
}
