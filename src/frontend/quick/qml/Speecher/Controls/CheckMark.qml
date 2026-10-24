import QtQuick
import Speecher.Theme

// The pencil disc with a tick that marks the chosen card: it pops in when
// shown and shrinks away when not.
Rectangle {
    property bool shown: true

    width: 18
    height: 18
    radius: width / 2
    color: Theme.accent
    scale: shown ? 1 : Theme.motionScale(0.2)
    opacity: shown ? 1 : 0
    Behavior on scale { Ease { duration: Theme.slow; curve: Theme.springy } }
    Behavior on opacity { Ease { duration: Theme.fast } }
    Icon { anchors.centerIn: parent; name: "check"; size: 12; color: Theme.accentText }
}
