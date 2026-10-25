import QtQuick
import Speecher.Theme

// The box every text input draws: a fill that lightens under the pointer
// and, with focus, turns to the page colour with a pencil border and a soft
// halo that grows in. Inputs set it as their background.
Rectangle {
    id: frame

    property bool focused: false
    property bool hovered: false

    radius: Theme.radius
    color: focused ? Theme.sheet : hovered ? Theme.surfaceHover : Theme.surface
    border.width: 1
    border.color: focused ? Theme.accent : hovered ? Theme.borderStrong : Theme.border
    Behavior on color { EaseColor {} }
    Behavior on border.color { EaseColor {} }

    // The halo: a softer, wider focus ring that shows for any focus, not
    // only the keyboard's.
    FocusRing { target: frame; shown: frame.focused; border.width: 3; border.color: Theme.accentSoft }
}
