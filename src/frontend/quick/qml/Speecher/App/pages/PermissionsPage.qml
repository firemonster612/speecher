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
        // the row says that too.
        HealthRow {
            rowId: "microphoneAccess"
            help: Health.failure(check) || (Audio.hasMicrophones || check.state !== "ok" ? Health.detail(check) : Schema.row("microphoneAccess").noMicrophoneHelp)
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
