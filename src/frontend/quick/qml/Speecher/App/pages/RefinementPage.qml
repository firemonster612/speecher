pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

Page {
    id: page
    title: Schema.page("refinement").title
    subtitle: Schema.page("refinement").subtitle

    readonly property string primary: Settings.values.refinementProvider || "none"
    readonly property var fallbacks: Settings.values.refinementFallbacks || []
    // Every provider a dictation may use, profiles' own included.
    readonly property var inUse: ModelText.refinersInUse()
    readonly property bool refines: inUse.length > 0
    // The providers that have settings on this page, in the Switcher's order.
    readonly property var configurable: ["openai", "anthropic", "local", "endpoint"]
    // The schema section of each one's settings, in the same order.
    readonly property var configurableSections: ["openAiModel", "anthropicModel", "localRunner", "refinementEndpointUrl"]
                                                 .map(id => Schema.row(id).section)
    // Which provider's settings show, when there are fallbacks.
    property string providerTab: primary
    // A search that lands on a provider's settings shows that provider's.
    function _follow(rowId) {
        const spec = Schema.row(rowId)
        const at = spec.page === "refinement" ? configurableSections.indexOf(spec.section) : -1
        if (at >= 0) providerTab = configurable[at]
    }
    Connections {
        target: Navigation
        function onPendingRowChanged() {
            page._follow(Navigation.pendingRow)
            if (page.promptRows.includes(Navigation.pendingRow)) advanced.open = true
        }
    }
    // The rows in the Advanced group, which opens for a search that lands
    // on one of them.
    readonly property var promptRows: Schema.rows.filter(r => r.page === "refinement" && r.section === Schema.row("customSystemPromptEnabled").section)
                                                 .map(r => r.id)

    // The page's fixed wording.
    readonly property var t: ({
        providerSettings: "Service settings",
        ultrafastPlans: "Ultrafast only works on ChatGPT Pro $500 and eligible Enterprise and Edu plans. Other plans run it at Standard.",
        ultrafastModels: "Ultrafast needs GPT-6 Astra or GPT-6.1 Sol. This model refines at Standard.",
        contextCaption: "Context helps it spell names and match what you're writing. Nothing is kept after the dictation.",
        promptCaption: "A custom system prompt. Most people keep the built-in rules.",
        customPrompt: "Custom prompt"
    })
    // Under the sample, which is an example: who would clean it up, or that
    // nothing does.
    readonly property string sampleCaption: {
        if (primary === "none") return "An example, pasted as spoken. Choose a service below to clean it up."
        const model = ModelText.refinementModel(primary)
        return "An example of cleanup by " + ModelText.provider(primary).label + (model ? " with " + model : "") + "."
    }

    Component.onCompleted: { Models.checkComputer(); ModelText.checkUnknownSignIns(); _follow(Navigation.pendingRow) }

    // The row takes a new primary out of the fallbacks, and None clears
    // them, in one save.
    function choose(id) {
        Settings.set("refinementProvider", id)
        providerTab = id
    }

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
        id: providerSection
        title: Schema.row("refinementProvider").label
        caption: Schema.row("refinementProvider").help
        SearchLanding { rowIds: ["refinementProvider", "refinementRating"]; target: providerSection }
        ServiceCards {
            options: States.withTones(ModelText.refinementChoices(Models.refinementProviders))
            currentId: page.primary
            unratedText: ModelText.unrated
            onActivated: id => page.choose(id)
        }
        // How to sign in to the chosen provider, under the cards as on
        // Speech to text, while its card says it isn't ready.
        RefinerSignInRow { providerId: page.primary }
    }

    // Settings for every provider a dictation may use: the chain's, and
    // those profiles pick for themselves.
    Reveal {
        open: page.inUse.length > 0

        Section {
            id: providerSettings
            readonly property string shown: page.inUse.includes(page.providerTab) ? page.providerTab : page.inUse[0] || ""
            title: page.inUse.length > 1 ? page.t.providerSettings : ModelText.provider(shown).label
            // With more than one, a switch between them.
            actions: [
                SegmentedControl {
                    id: providerTabs
                    objectName: "providerTabs"
                    options: page.inUse.map(id => ({ id: id, label: ModelText.provider(id).label }))
                    currentId: providerSettings.shown
                    onActivated: id => page.providerTab = id
                    enabled: page.inUse.length > 1
                    opacity: enabled ? 1 : 0
                    Behavior on opacity { Ease {} }
                }
            ]

            Switcher {
                currentIndex: Math.max(0, page.configurable.indexOf(providerSettings.shown))

                Column {
                    SchemaRow {
                        rowId: "openAiModel"
                        SuggestField {
                            suggestionsText: Words.suggestions
                            text: Settings.values.openAiModel || ""
                            suggestions: Models.openAiModels
                            onCommitted: v => Settings.set("openAiModel", v)
                        }
                    }
                    SchemaRow { rowId: "openAiEffort" }
                    SchemaRow { rowId: "openAiSpeed" }
                    Reveal {
                        open: Settings.values.openAiSpeed === "ultrafast"
                        Notice {
                            indicator: "icon"
                            variant: "small"
                            inset: Theme.rowInset
                            text: Models.supportsUltrafast(Settings.values.openAiModel || "")
                                  ? page.t.ultrafastPlans
                                  : page.t.ultrafastModels
                        }
                    }
                }

                Column {
                    SchemaRow {
                        rowId: "anthropicModel"
                        SuggestField {
                            suggestionsText: Words.suggestions
                            text: Settings.values.anthropicModel || ""
                            suggestions: Models.anthropicModels
                            onCommitted: v => Settings.set("anthropicModel", v)
                        }
                    }
                    SchemaRow { rowId: "anthropicEffort" }
                    SchemaRow { rowId: "anthropicFastMode" }
                }

                LocalRunnerRows {}
                RefinementEndpointRows {}
            }
            // The system prompt, which few people change, folded away
            // under the provider's settings until opened or searched for.
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

    // Fallbacks, full width as on Speech to text, while a provider refines.
    Reveal {
        open: page.refines && page.primary !== "none"
        Section {
            id: fallbackSection
            // Headed and worded as Speech to text's list is.
            title: Schema.row("refinementFallbacks").headingText.arg(ModelText.provider(page.primary).label)
            caption: Schema.row("refinementFallbacks").help
            SearchLanding { rowIds: ["refinementFallbacks"]; target: fallbackSection }
            FallbackList {
                moveUpText: Words.moveUp
                moveDownText: Words.moveDown
                removeText: Words.remove
                ids: page.fallbacks
                options: Models.refinementProviders.filter(p => p.id !== page.primary).map(p => ({ id: p.id, label: p.label }))
                statusOf: (id, index) => {
                    const s = ModelText.fallbackStatus(ModelText.provider(id), index, ModelText.provider(page.primary).label)
                    return { text: s.text, tone: States.tone(s.state) }
                }
                emptyText: Schema.row("refinementFallbacks").emptyText.arg(ModelText.provider(page.primary).label)
                addLabel: Schema.row("refinementFallbacks").addLabel
                addHelp: Schema.row("refinementFallbacks").addHelp
                addPlaceholder: Words.choose
                onEdited: ids => Settings.set("refinementFallbacks", ids)
            }
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

    SubpageRows { pageId: "refinement" }
}
