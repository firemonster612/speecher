import QtQuick
import Speecher.Controls
import Speecher.App

// Set up the virtual keyboard that types for Speecher, or settle for the
// clipboard. Setting it up here pastes Automatically.
StepPage {
    id: page

    Card {
        HealthRow { id: keyboard; rowId: "virtualKeyboardHealth"; fromSetup: true }
        VirtualKeyboardTest {}
        // Clipboard only is the Default paste rule, which finishing setup
        // saves. A working keyboard leaves nothing to settle for.
        SettingRow {
            shown: keyboard.check.state !== "ok"
            title: Setup.text.clipboardInstead
            help: Setup.text.clipboardInsteadHelp
            Switch { checked: page.choices.clipboardOnly; onToggled: page.choices.clipboardOnly = checked }
        }
    }
}
