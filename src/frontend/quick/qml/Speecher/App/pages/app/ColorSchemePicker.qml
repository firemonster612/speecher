import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.App

// The Colors setting as small windows in the current theme: the desktop's
// colours and Speecher's own in one row, then the schemes installed on the
// desktop under their own heading, when there are any.
Column {
    id: root

    property string currentId
    // The desktop's own colours' name, and the heading over the installed
    // schemes.
    property string systemLabel
    property string installedTitle
    signal activated(string id)

    readonly property int _columns: 7
    spacing: Theme.space4

    component SchemeThumb: MiniWindow {
        property var option
        scheme: Theme.schemeOf(option.source, Theme.dark)
    }

    LookPicker {
        width: parent.width
        columns: root._columns
        options: [Object.assign({ label: root.systemLabel }, ColorSchemes.system)].concat(ColorSchemes.presets)
        currentId: root.currentId
        thumbnail: SchemeThumb {}
        onActivated: id => root.activated(id)
    }
    Reveal {
        width: parent.width
        open: ColorSchemes.installed.length > 0
        Column {
            width: parent.width
            spacing: Theme.space2
            Txt { text: root.installedTitle; variant: "small"; strong: true; tone: "secondary" }
            LookPicker {
                width: parent.width
                columns: root._columns
                options: ColorSchemes.installed
                currentId: root.currentId
                thumbnail: SchemeThumb {}
                onActivated: id => root.activated(id)
            }
        }
    }
}
