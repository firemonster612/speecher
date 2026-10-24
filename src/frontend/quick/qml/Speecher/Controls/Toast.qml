import QtQuick
import Speecher.Theme

// The window's toast: it rises in at the bottom centre when something posts
// to Toaster, leaves on its own after a while (not while the pointer is on
// it), and a new toast rolls its words into the one already showing rather
// than stacking. Put one in the window, over everything else.
Item {
    id: toast

    property bool shown: false
    property var current: ({ text: "", icon: "", tone: "neutral", actionText: "", action: null })

    function dismiss() { shown = false }

    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: Theme.space6
    width: body.width
    height: body.height
    z: 100

    Connections {
        target: Toaster
        function onPosted(next) {
            toast.current = next
            toast.shown = true
            hold.restart()
        }
    }
    Timer { id: hold; interval: Theme.toastTime; running: toast.shown && !hover.hovered; onTriggered: toast.dismiss() }

    Item {
        id: body
        width: row.width + Theme.space4 * 2
        height: 44
        opacity: toast.shown ? 1 : 0
        visible: opacity > 0
        scale: toast.shown ? 1 : Theme.motionScale(0.96)
        transform: Translate { y: toast.shown ? 0 : 14 * Theme.movement; Behavior on y { Ease { duration: toast.shown ? Theme.slow : Theme.base; curve: toast.shown ? Theme.easeOut : Theme.easeIn } } }
        Behavior on opacity { Ease { duration: toast.shown ? Theme.base : Theme.fast } }
        Behavior on scale { Ease { duration: Theme.slow } }
        Behavior on width { Ease { duration: Theme.slow } }

        Surface { anchors.fill: parent; radius: Theme.radiusLarge }
        Row {
            id: row
            x: Theme.space4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space2
            Icon {
                visible: toast.current.icon.length > 0
                name: toast.current.icon
                size: 16
                color: toast.current.tone === "neutral" ? Theme.textSecondary : Theme.tone(toast.current.tone)
                anchors.verticalCenter: parent.verticalCenter
            }
            MorphText { text: toast.current.text; variant: "label"; anchors.verticalCenter: parent.verticalCenter }
            Button {
                visible: toast.current.actionText.length > 0
                text: toast.current.actionText
                variant: "ghost"
                size: "small"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: { if (toast.current.action) toast.current.action(); toast.dismiss() }
            }
        }
        HoverHandler { id: hover }
    }
}
