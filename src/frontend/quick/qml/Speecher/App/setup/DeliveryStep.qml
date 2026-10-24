import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// Set up the virtual keyboard that types for Speecher, or settle for the
// clipboard.
StepPage {
    id: page

    Card {
        HealthRow { rowId: "virtualKeyboardHealth" }
        // Clipboard only is a Default paste rule, which the app does not save
        // yet, so setup offers it only where it would be kept.
        SettingRow {
            enabled: Settings.saves("globalPasteRule")
            title: System.setupText.clipboardInstead
            help: System.setupText.clipboardInsteadHelp
            Switch { checked: page.choices.clipboardOnly; onToggled: page.choices.clipboardOnly = checked }
        }
    }
}
