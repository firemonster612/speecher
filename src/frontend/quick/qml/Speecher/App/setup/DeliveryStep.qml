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
// setup saves; one always stays on. Under them on Linux and macOS, what
// accessibility does besides, with its own fix only where no step that is
// on asks for it. Where nothing that is on can deliver yet, the step offers
// to leave the words on the clipboard instead.
StepPage {
    id: page

    readonly property var _on: ["insert", "press", "keep"].filter(id => page.choices[id])
    readonly property var _accessibility: Health.find(System.checks, "desktopAccessibility")
    readonly property bool _accessibilityAsked: System.deliverySteps
        .some(s => page.choices[s.id] && (s.needs || []).includes("accessibility"))
    readonly property bool _delivers: (page.choices.steps.find(s => s.id === "delivery") || {}).done

    component StepChoice: DeliveryStepRow {
        draft: true
        on: !!page.choices[spec.step]
        stepsOn: page._on
        onSwitched: checked => page.choices[spec.step] = checked
    }

    Card {
        StepChoice { rowId: "insertThroughAccessibility" }
        StepChoice { rowId: "pressPasteKeys" }
        VirtualKeyboardTest {}
        StepChoice { rowId: "keepOnClipboard" }
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

    // The reduced choice, while nothing that is on delivers and the
    // clipboard can hold the words.
    Reveal {
        open: !page._delivers && page.choices.stepRuns("keep")
        width: parent.width
        Card {
            SettingRow {
                title: Setup.text.clipboardOnly
                help: Setup.text.clipboardOnlyHelp
                Button {
                    text: Setup.text.clipboardOnly
                    size: "small"
                    onClicked: page.choices.continueWithClipboardOnly()
                }
            }
        }
    }
}
