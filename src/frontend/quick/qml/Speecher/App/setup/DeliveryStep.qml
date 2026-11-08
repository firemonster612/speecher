pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../pages/text"

// Choose the delivery steps, each with what it relies on here and its fix,
// as on the Pasting page: accessibility on Insert (and Press on macOS), the
// virtual keyboard on Press on Linux. The steps are drafts, which finishing
// setup saves; one always stays on, and each row shows what the drafts
// need (SetupChoices.delivery). Under them on Linux and macOS, what
// accessibility does besides, with its own fix only where no step that is
// on asks for it; on Windows, that the steps need nothing.
StepPage {
    id: page

    readonly property var _steps: page.choices.delivery.steps
    readonly property var _accessibility: Health.find(System.checks, "desktopAccessibility")
    readonly property bool _accessibilityAsked: _steps.some(s => s.on && (s.needs || []).includes("accessibility"))

    component StepChoice: DeliveryStepRow {
        draft: true
        steps: page._steps
        onSwitched: checked => page.choices[spec.step] = checked
    }

    Card {
        StepChoice { rowId: "insertThroughAccessibility" }
        StepChoice { rowId: "pressPasteKeys" }
        VirtualKeyboardTest {}
        StepChoice { rowId: "keepOnClipboard" }
    }

    Txt {
        visible: Schema.pasting.noNeeds !== ""
        width: parent.width
        text: Schema.pasting.noNeeds
        variant: "small"
        tone: "secondary"
        wrapMode: Text.WordWrap
    }

    Reveal {
        open: Qt.platform.os !== "windows" && page._accessibility.reason !== "unsupported"
        width: parent.width
        Row {
            width: parent.width
            spacing: Theme.space3
            Txt {
                width: parent.width - (fixAccessibility.visible ? fixAccessibility.width + parent.spacing : 0)
                text: Setup.text.accessibilityAlso
                variant: "small"
                tone: "secondary"
                wrapMode: Text.WordWrap
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                id: fixAccessibility
                visible: !page._accessibilityAsked && !!page._accessibility.fix && page._accessibility.state !== "working"
                text: Schema.pasting.needs.accessibility.fix || Health.fix(page._accessibility)
                size: "small"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: System.fix("desktopAccessibility")
            }
        }
    }
}
