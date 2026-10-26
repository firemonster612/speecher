import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import Speecher.Theme
import Speecher.Controls
import Speecher.App

// The first-run assistant: the steps down the side, the current one on a
// panel beside them. Moving forward slides the next step in from the right,
// going back slides it in from the left.
ClientWindow {
    id: window
    width: 960
    // Tall enough for the longest step, Refinement, to show whole.
    height: 764
    minimumWidth: 860
    minimumHeight: 600
    visible: true
    title: Setup.text.title

    // The step on show, from 0.
    property int step: 0

    readonly property var steps: choices.steps
    readonly property bool _last: step === steps.length - 1
    // The furthest step the gates let anyone reach.
    readonly property int _limit: steps.findIndex(s => !s.done) < 0 ? steps.length - 1 : steps.findIndex(s => !s.done)
    readonly property var _files: ({
        welcome: "WelcomeStep.qml", transcription: "TranscriptionStep.qml", microphone: "MicrophoneStep.qml",
        accessibility: "AccessibilityStep.qml", delivery: "DeliveryStep.qml", refinement: "RefinementStep.qml",
        shortcut: "ShortcutStep.qml", ready: "ReadyStep.qml"
    })

    // What the steps choose and where each stands. Finishing setup writes
    // the choices to Settings.
    SetupChoices {
        id: choices
        objectName: "setupChoices"
        onGoTo: stepId => window.go(window.steps.findIndex(s => s.id === stepId))
    }

    function go(index) { if (choices.reachable(index)) step = Math.max(0, Math.min(steps.length - 1, index)) }
    // Finish writes the choices, says setup is done, then closes; closing
    // any other way leaves setup to run again.
    signal finished()
    function finish() { choices.finish(); finished(); close() }
    onStepChanged: stack.show()

    // Steps ------------------------------------------------------------
    Item {
        id: sideColumn
        width: 248
        height: parent.height

        // In the title strip, which moves the window.
        Brand { id: brand; title: Setup.text.title }

        // The steps' nodes line up under the mark, or on macOS under the
        // close light.
        StepRail {
            x: Theme.titleBarInset
            y: brand.height + Theme.space5
            width: parent.width - x - Theme.space6
            steps: window.steps
            current: window.step
            limit: window._limit
            onPicked: index => window.go(index)
        }

        Txt {
            x: Theme.sideInset
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 26
            width: parent.width - Theme.sideInset * 2
            text: Setup.text.runAgain
            variant: "small"
            tone: "tertiary"
            wrapMode: Text.WordWrap
        }
    }

    // The current step -------------------------------------------------
    Sheet {
        side: sideColumn

        T.StackView {
            id: stack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: footer.top
            anchors.margins: 1
            clip: true

            // 1 moving forward, -1 moving back.
            property int direction: 1
            property int shown: -1

            function show() {
                direction = window.step >= shown ? 1 : -1
                shown = window.step
                const info = window.steps[window.step]
                const props = { info: info, choices: choices }
                const url = Qt.resolvedUrl(window._files[info.id])
                if (depth === 0) push(url, props, T.StackView.Immediate)
                else replace(null, url, props)
            }
            Component.onCompleted: show()

            replaceEnter: Transition {
                ParallelAnimation {
                    Ease { property: "x"; from: 56 * stack.direction * Theme.movement; to: 0; duration: Theme.page }
                    Ease { property: "opacity"; from: 0; to: 1; duration: Theme.slow }
                }
            }
            replaceExit: Transition {
                ParallelAnimation {
                    Ease { property: "x"; to: -40 * stack.direction * Theme.movement; curve: Theme.easeIn }
                    Ease { property: "opacity"; to: 0; duration: Theme.fast; curve: Theme.easeIn }
                }
            }
        }

        Item {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 68
            // In line with the step's content above.
            Rectangle { x: Theme.pageInset; width: parent.width - Theme.pageInset * 2; height: 1; color: Theme.rule }

            // Skip and Back fade where they stand rather than reflowing the
            // footer; the main button's label rolls to its new wording. Skip
            // only passes steps that are already done, never goes around one.
            Button {
                x: Theme.pageInset
                anchors.verticalCenter: parent.verticalCenter
                text: Setup.text.skip
                variant: "ghost"
                enabled: !window._last && window._limit === window.steps.length - 1
                onClicked: window.finish()
                opacity: enabled ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease {} }
            }
            // Why Continue waits, where Skip would stand.
            Txt {
                x: Theme.pageInset
                anchors.verticalCenter: parent.verticalCenter
                text: Setup.text.waits
                tone: "secondary"
                opacity: !window._last && window._limit === window.step ? 1 : 0
                visible: opacity > 0
                Behavior on opacity { Ease {} }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.pageInset
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space2
                Button {
                    text: Setup.text.back
                    enabled: window.step > 0
                    opacity: enabled ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity { Ease {} }
                    onClicked: window.go(window.step - 1)
                }
                Button {
                    text: window.step === 0 ? Setup.text.getStarted : window._last ? Setup.text.startDictating : Setup.text.continue_
                    variant: "primary"
                    enabled: window._limit > window.step || window._last && window._limit === window.step
                    onClicked: window._last ? window.finish() : window.go(window.step + 1)
                }
            }
        }
    }

    Shortcut { sequences: ["Alt+Right"]; onActivated: window.go(window.step + 1) }
    Shortcut { sequences: ["Alt+Left"]; onActivated: window.go(window.step - 1) }
}
