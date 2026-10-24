import QtQuick
import Speecher.Theme

// A stand-in for an application's icon: its initial on a colour picked from
// its name, or a Lucide icon when iconName is set.
Rectangle {
    id: root
    property string name
    property string iconName
    property int size: 28

    readonly property color _ink: {
        let h = 0
        for (let i = 0; i < name.length; ++i) h = (h * 31 + name.charCodeAt(i)) >>> 0
        return Theme.avatarInks[h % Theme.avatarInks.length]
    }

    width: size
    height: size
    radius: Math.round(size * 0.28)
    color: root._ink
    Txt {
        visible: root.iconName.length === 0
        anchors.centerIn: parent
        text: root.name.length ? root.name[0].toUpperCase() : "?"
        font.pixelSize: Math.round(root.size * 0.46)
        font.weight: Font.Bold
        color: Theme.onAvatar
    }
    Icon { visible: root.iconName.length > 0; anchors.centerIn: parent; name: root.iconName; size: Math.round(root.size * 0.55); color: Theme.onAvatar }
}
