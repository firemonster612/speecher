pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "app"

Page {
    id: page
    title: Schema.page("appearance").title
    subtitle: Schema.page("appearance").subtitle

    readonly property var t: ({
        systemColors: "System",
        // Desktop schemes follow the theme too, so a dark one shows light in
        // the Light theme; the heading says so.
        installedSchemes: "On this computer, following the theme"
    })

    Section {
        SchemaRow {
            rowId: "themeControl"
            ThemePicker {
                options: Schema.row("themeControl").options
                currentId: Settings.values.themeControl || "system"
                // The window applies it, following the desktop on System.
                onActivated: id => Settings.set("themeControl", id)
            }
        }
        SchemaRow {
            rowId: "colorScheme"
            stacked: true
            ColorSchemePicker {
                width: parent.width
                currentId: Settings.values.colorScheme || "steno"
                systemLabel: page.t.systemColors
                installedTitle: page.t.installedSchemes
                onActivated: id => Settings.set("colorScheme", id)
            }
        }
        SchemaRow {
            rowId: "windowLayout"
            LookPicker {
                width: 268
                options: Schema.row("windowLayout").options
                currentId: Settings.values.windowLayout || "framed"
                thumbnail: MiniWindow {
                    property var option
                    split: option.id === "split"
                }
                onActivated: id => Settings.set("windowLayout", id)
            }
        }
        SchemaRow { rowId: "translucency" }
        SchemaRow { rowId: "roundedCorners" }
    }
}
