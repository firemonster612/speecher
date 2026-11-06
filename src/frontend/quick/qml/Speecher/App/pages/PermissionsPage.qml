import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

Page {
    title: Schema.page("permissions").title
    subtitle: Schema.page("permissions").subtitle

    Section {
        caption: Health.summary(System.checks)
        // Access says nothing of whether there is a microphone, so with none
        // the row warns of that, in Dictation's words.
        HealthRow {
            rowId: "microphoneAccess"
            readonly property bool noMicrophone: !Health.failure(check) && !Audio.hasMicrophones && check.state === "ok"
            status: noMicrophone ? Schema.row("audioDevice").noneLabel : Health.status(check)
            statusTone: noMicrophone ? "warning" : States.tone(check.state ?? "")
            help: Health.failure(check) || (noMicrophone ? Schema.row("microphoneAccess").noMicrophoneHelp : Health.detail(check))
            // The page it names is a link to it.
            helpLink: noMicrophone ? Schema.page("dictation").title + " › " + Schema.page("microphone").title : ""
            onHelpLinkActivated: Navigation.go("microphone")
        }
        // Where Speecher keeps it on, the help says how Chromium apps join
        // in, and a menu, as the virtual keyboard's on Pasting, undoes it.
        HealthRow {
            rowId: "desktopAccessibility"
            help: Health.failure(check) || (System.canDisablePermanentAccessibility
                                            ? Schema.row("desktopAccessibility").keptOnHelp : Health.detail(check))
            Reveal {
                sideways: true
                open: System.canDisablePermanentAccessibility
                anchors.verticalCenter: parent.verticalCenter
                IconButton {
                    iconName: "ellipsis"
                    size: 28
                    tooltip: Words.more
                    onClicked: accessibilityMenu.open()
                    Menu {
                        id: accessibilityMenu
                        width: 240
                        items: [{ id: "disable", label: "Stop keeping on", icon: "power-off",
                                  help: "Turns off when you sign out. Chromium apps stop once restarted." }]
                        onPicked: System.disablePermanentAccessibility()
                    }
                }
            }
        }
        HealthRow { rowId: "globalShortcuts" }
        HealthRow { rowId: "virtualKeyboardHealth" }
        VirtualKeyboardTest {}
    }
    // Not a check, so on its own card under the ones the caption counts.
    Section {
        SchemaRow { rowId: "runSetup" }
    }
}
