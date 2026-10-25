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
        HealthRow { rowId: "microphoneAccess" }
        HealthRow { rowId: "desktopAccessibility" }
        HealthRow { rowId: "globalShortcuts" }
        HealthRow { rowId: "virtualKeyboardHealth" }
        VirtualKeyboardTest {}
        SchemaRow { rowId: "runSetup" }
    }
}
