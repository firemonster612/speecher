import QtQuick
import Speecher.Controls
import Speecher.App

// Set up the virtual keyboard that types for Speecher, or settle for the
// clipboard.
StepPage {
    id: page

    Card {
        HealthRow { rowId: "virtualKeyboardHealth" }
        VirtualKeyboardTest {}
        // Clipboard only is the Default paste rule, which finishing setup saves.
        SettingRow {
            title: Setup.text.clipboardInstead
            help: Setup.text.clipboardInsteadHelp
            Switch { checked: page.choices.clipboardOnly; onToggled: page.choices.clipboardOnly = checked }
        }
    }
}
