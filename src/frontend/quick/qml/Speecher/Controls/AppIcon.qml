import QtQuick
import Speecher.Theme

// An application's icon: the image at source (an installed app's, through
// image://appicon), or until it loads, or where there is none, the app's
// initial on a colour picked from its name, or a Lucide icon when iconName
// is set.
Rectangle {
    id: root
    property string name
    property string iconName
    property url source
    property int size: 28

    // image://appicon answers a single pixel for an app without an icon.
    readonly property bool _imaged: image.status === Image.Ready && image.implicitWidth > 1
    readonly property color _ink: {
        let h = 0
        for (let i = 0; i < name.length; ++i) h = (h * 31 + name.charCodeAt(i)) >>> 0
        return Theme.avatarInks[h % Theme.avatarInks.length]
    }

    width: size
    height: size
    radius: Math.round(size * 0.28)
    color: _imaged ? "transparent" : root._ink
    Behavior on color { EaseColor {} }
    Txt {
        visible: root.iconName.length === 0
        opacity: root._imaged ? 0 : 1
        Behavior on opacity { Ease {} }
        anchors.centerIn: parent
        text: root.name.length ? root.name[0].toUpperCase() : "?"
        font.pixelSize: Math.round(root.size * 0.46)
        font.weight: Font.Bold
        color: Theme.onAvatar
    }
    Icon { visible: root.iconName.length > 0; anchors.centerIn: parent; name: root.iconName; size: Math.round(root.size * 0.55); color: Theme.onAvatar }
    Image {
        id: image
        anchors.fill: parent
        source: root.source
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: root._imaged ? 1 : 0
        Behavior on opacity { Ease {} }
    }
}
