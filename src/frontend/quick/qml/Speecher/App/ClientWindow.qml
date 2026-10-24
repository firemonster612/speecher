import QtQuick
import QtQuick.Window
import QtQuick.Effects
import Speecher.Theme
import Speecher.Backend

// A window that draws its own frame, the same on every desktop: rounded
// corners and a soft shadow, a strip along the top that moves it (a double
// click maximizes), its buttons at the top right, and a resize grip in the
// shadow margin. Maximized or full screen, the margin and corners go.
// Content goes inside and should leave the top titleBarHeight free of
// controls, since the strip lies over it. While Theme is translucent, the
// frame's paper lets the desktop through, blurred by the compositor.
Window {
    id: client

    default property alias content: body.data
    readonly property bool framed: visibility !== Window.Maximized && visibility !== Window.FullScreen
    readonly property int margin: framed ? Theme.windowShadow : 0
    readonly property int cornerRadius: framed ? Theme.radiusWindow : 0
    readonly property int titleBarHeight: Theme.titleBarHeight

    flags: Qt.Window | Qt.FramelessWindowHint
    color: "transparent"

    function toggleMaximized() { visibility === Window.Maximized ? showNormal() : showMaximized() }

    // What the compositor blurs: the frame, without the shadow margin.
    readonly property var _blur: ({
        area: Theme.translucent ? Qt.rect(margin, margin, width - margin * 2, height - margin * 2) : Qt.rect(0, 0, 0, 0),
        radius: cornerRadius,
        dark: Theme.dark
    })
    on_BlurChanged: Appearance.setBlurBehind(client, _blur.area, _blur.radius, _blur.dark)
    Component.onCompleted: Appearance.setBlurBehind(client, _blur.area, _blur.radius, _blur.dark)

    RectangularShadow {
        visible: client.framed
        anchors.fill: frame
        radius: frame.radius
        blur: 24
        offset.y: 4
        color: Theme.shadow
    }
    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: client.margin
        radius: client.cornerRadius
        color: Theme.sidePaper

        Item {
            id: body
            anchors.fill: parent
        }
        // The frame's edge, over the content so nothing paints across it.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: client.framed ? 1 : 0
            border.color: Theme.sheetBorder
        }

        // The strip that moves the client.
        Item {
            width: parent.width
            height: client.titleBarHeight
            DragHandler {
                target: null
                grabPermissions: PointerHandler.TakeOverForbidden
                onActiveChanged: if (active) client.startSystemMove()
            }
            TapHandler {
                gesturePolicy: TapHandler.DragThreshold
                onDoubleTapped: client.toggleMaximized()
            }
        }

        WindowControls {
            window: client
            anchors.right: parent.right
            anchors.rightMargin: Theme.titleBarInset
            y: (client.titleBarHeight - height) / 2
        }
    }

    // The resize grip: the shadow margin along each edge, and a little
    // further in at the corners, where the frame is rounded away.
    component Grip: MouseArea {
        property int edges
        acceptedButtons: Qt.LeftButton
        onPressed: Window.window.startSystemResize(edges)
    }
    Item {
        id: grips
        readonly property int edge: client.margin
        readonly property int corner: client.margin + Theme.radiusWindow
        anchors.fill: parent
        visible: client.framed

        Grip { edges: Qt.TopEdge; cursorShape: Qt.SizeVerCursor
               x: grips.corner; width: grips.width - grips.corner * 2; height: grips.edge }
        Grip { edges: Qt.BottomEdge; cursorShape: Qt.SizeVerCursor
               x: grips.corner; y: grips.height - height; width: grips.width - grips.corner * 2; height: grips.edge }
        Grip { edges: Qt.LeftEdge; cursorShape: Qt.SizeHorCursor
               y: grips.corner; width: grips.edge; height: grips.height - grips.corner * 2 }
        Grip { edges: Qt.RightEdge; cursorShape: Qt.SizeHorCursor
               x: grips.width - width; y: grips.corner; width: grips.edge; height: grips.height - grips.corner * 2 }
        Grip { edges: Qt.TopEdge | Qt.LeftEdge; cursorShape: Qt.SizeFDiagCursor
               width: grips.corner; height: grips.corner }
        Grip { edges: Qt.TopEdge | Qt.RightEdge; cursorShape: Qt.SizeBDiagCursor
               x: grips.width - width; width: grips.corner; height: grips.corner }
        Grip { edges: Qt.BottomEdge | Qt.LeftEdge; cursorShape: Qt.SizeBDiagCursor
               y: grips.height - height; width: grips.corner; height: grips.corner }
        Grip { edges: Qt.BottomEdge | Qt.RightEdge; cursorShape: Qt.SizeFDiagCursor
               x: grips.width - width; y: grips.height - height; width: grips.corner; height: grips.corner }
    }
}
