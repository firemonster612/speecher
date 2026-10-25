pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme

// A small picture of the main window in one scheme and layout: paper with a
// few sidebar entries, and a page with a title over a raised card of two
// rows, one with a pencil switch. Framed, the page is a sheet inset in the
// paper; split, it runs to the edges beside a rule.
Rectangle {
    id: mini
    // The six roles, as Theme.schemeOf gives them.
    property var scheme: Theme.scheme
    property bool split: Theme.split

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
        id: page
        x: mini.split ? 40 : 42
        y: mini.split ? 0 : 8
        width: mini.width - x - (mini.split ? 0 : 8)
        height: mini.height - (mini.split ? 0 : 16)
        topLeftRadius: mini.split ? 0 : Theme.radius
        bottomLeftRadius: mini.split ? 0 : Theme.radius
        topRightRadius: mini.split ? mini.radius : Theme.radius
        bottomRightRadius: mini.split ? mini.radius : Theme.radius
        color: mini.scheme.sheet
        Rectangle { visible: mini.split; width: 1; height: parent.height; color: Theme.soft(mini.scheme.text, 0.12) }
        Rectangle { x: 8; y: mini.split ? 13 : 9; width: parent.width * 0.4; height: 5; radius: 2.5; color: mini.scheme.text }
        Rectangle {
            id: card
            x: 8; y: mini.split ? 25 : 21
            width: parent.width - 16; height: parent.height - y - 8
            radius: Theme.radiusSmall
            color: mini.scheme.raised
            border.width: 1
            border.color: mini.scheme.rule || Theme.soft(mini.scheme.text, 0.12)
            Repeater {
                model: 2
                Rectangle {
                    required property int index
                    x: 6; y: card.height * (index + 0.5) / 2 - 1.5
                    width: card.width * 0.45; height: 3; radius: 1.5
                    color: Theme.soft(mini.scheme.text, 0.35)
                }
            }
            Rectangle { x: 6; y: card.height / 2; width: card.width - 12; height: 1; color: Theme.soft(mini.scheme.text, 0.12) }
            Rectangle {
                x: card.width - 20; y: card.height / 4 - 3.5
                width: 14; height: 7; radius: 3.5
                color: mini.scheme.accent
            }
        }
    }
}
