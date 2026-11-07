import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/text"

// Whether accessibility is on (granted, on macOS), with the fix if it is
// not. The step's description says what it lets Speecher do; where the
// delivery steps chosen don't need it, the row says so.
StepPage {
    id: page

    readonly property var need: Schema.row("insertThroughAccessibility").need

    Card {
        SettingRow {
            title: page.need.label
            help: status.failure || (page.choices.accessibilityNeeded ? Health.detail(status.check)
                                     : Setup.text.notNeeded + ". It still lets cleanup read the text around your cursor.")
            NeedStatus {
                id: status
                need: page.need
                ready: System.accessibility
                quiet: !page.choices.accessibilityNeeded
            }
        }
    }
}
