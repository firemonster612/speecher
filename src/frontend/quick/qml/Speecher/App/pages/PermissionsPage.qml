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
        HealthRow { rowId: "desktopAccessibility" }
        HealthRow { rowId: "globalShortcuts" }
        HealthRow { rowId: "virtualKeyboardHealth" }
        VirtualKeyboardTest {}
    }
    // Not a check, so on its own card under the ones the caption counts.
    Section {
        SchemaRow { rowId: "runSetup" }
    }
}
