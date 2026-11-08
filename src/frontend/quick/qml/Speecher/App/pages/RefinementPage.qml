pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// What refinement does, shown on a sample; Clean up my words; the services
// that clean up, in the order Speecher tries them, each with its model and
// ChatGPT's and Claude's thinking and speed; then what refinement can see
// and the system prompt. How each signs in, the Local Runner and the server
// are on Services; a service that can't run offers Fix in its row.
Page {
    id: page
    title: Schema.page("refinement").title
    subtitle: Schema.page("refinement").subtitle

    readonly property bool cleansUp: Settings.values.refinementEnabled !== false
    readonly property string primary: ModelText.mainRefiner
    // Every service a dictation may use, profiles' own included.
    readonly property bool refines: ModelText.refinersInUse().length > 0
    readonly property var orderSpec: Schema.row("refinementServices")

    Connections {
        target: Navigation
        function onPendingRowChanged() {
            if (page.promptRows.includes(Navigation.pendingRow)) advanced.open = true
        }
    }
    // The rows in the Advanced group, which opens for a search that lands
    // on one of them.
    readonly property var promptRows: Schema.rows.filter(r => r.page === "refinement" && r.section === Schema.row("customSystemPromptEnabled").section)
                                                 .map(r => r.id)

    // The page's fixed wording.
    readonly property var t: ({
        contextCaption: "Context helps it spell names and match what you're writing. Nothing is kept after the dictation.",
        promptCaption: "A custom system prompt. Most people keep the built-in rules.",
        customPrompt: "Custom prompt"
    })
    // Under the sample, which is an example: who would clean it up, or that
    // nothing does.
    readonly property string sampleCaption: {
        if (primary === "none") return "An example, pasted as spoken. Turn on Clean up my words to clean it up."
        const model = ModelText.refinementModel(primary)
        return "An example of cleanup by " + ModelText.provider(primary).label + (model ? " with " + model : "") + "."
    }

    Component.onCompleted: { Models.checkComputer(); ModelText.checkUnknownSignIns() }

    // What refinement does to a sentence, marked like a proof. With no
    // provider the marks fade and it reads as it will be pasted.
    Section {
        plain: true
        Plate {
            height: sample.implicitHeight + Theme.space4 * 2
            Column {
                id: sample
                x: Theme.space6
                y: Theme.space4
                width: parent.width - Theme.space6 * 2
                spacing: Theme.space3
                DiffText {
                    segments: ModelText.refinementSample
                    mode: page.primary === "none" ? "spoken" : "marked"
                    variant: "lead"
                }
                MorphText {
                    text: page.sampleCaption
                    variant: "small"
                    tone: "secondary"
                }
            }
        }
    }

    Section {
        SchemaRow { rowId: "refinementEnabled" }
    }

    Reveal {
        open: page.cleansUp
        Section {
            id: servicesSection
            title: page.orderSpec.label
            caption: page.orderSpec.help
            SearchLanding { rowIds: ["refinementServices"]; target: servicesSection }
            ServiceOrderList { role: "refinement" }
        }
    }

    Reveal {
        open: page.refines
        Section {
            id: contextSection
            title: Schema.row("targetContextControl").section
            caption: page.t.contextCaption
            AccessibilityNotice { text: Schema.row("targetContextControl").disabledHelp }
            SchemaRow { rowId: "targetContextControl" }
            SchemaRow { id: screenshotRow; rowId: "includeScreenshotContext" }
            // macOS asks before Speecher captures the screen, a check only
            // while screenshots are on there.
            HealthRow {
                rowId: "screenRecording"
                shown: !!check.id && screenshotRow.enabled
            }
        }
    }

    // The system prompt, which few people change, folded away until opened
    // or searched for.
    Reveal {
        open: page.refines
        Section {
            Disclosure {
                id: advanced
                title: Schema.row("customSystemPromptEnabled").section
                help: page.t.promptCaption
                trailingText: Settings.values.customSystemPromptEnabled ? page.t.customPrompt : ""
                Component.onCompleted: open = page.promptRows.includes(Navigation.pendingRow)
                SchemaRow { rowId: "customSystemPromptEnabled" }
                Reveal {
                    open: !!Settings.values.customSystemPromptEnabled
                    SchemaRow {
                        rowId: "customSystemPrompt"
                        stacked: true
                        TextArea {
                            width: parent.width
                            mono: true
                            minHeight: 150
                            text: Settings.values.customSystemPrompt || Models.builtInSystemPrompt
                            onEditingFinished: Settings.set("customSystemPrompt", text)
                        }
                    }
                    SchemaRow {
                        id: resetPromptRow
                        rowId: "resetCustomSystemPrompt"
                        Button {
                            text: resetPromptRow.spec.actionLabel
                            iconName: "rotate-ccw"
                            size: "small"
                            enabled: !!Settings.values.customSystemPrompt && Settings.values.customSystemPrompt !== Models.builtInSystemPrompt
                            onClicked: Settings.set("customSystemPrompt", "")
                        }
                    }
                }
            }
        }
    }

    SubpageRows { pageId: "refinement" }
}
