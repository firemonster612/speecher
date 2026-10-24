pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

Page {
    id: page
    title: Schema.page("refinement").title
    subtitle: Schema.page("refinement").subtitle

    readonly property string primary: Settings.values.refinementProvider || "none"
    readonly property var fallbacks: Settings.values.refinementFallbacks || []
    // Every provider a dictation may use, in the order it tries them.
    readonly property var chain: primary === "none" ? [] : [primary].concat(fallbacks)
    // Profiles can pick their own provider, which runs even while this page
    // says None. Each provider once, the chain's first.
    readonly property var inUse: chain.concat(Library.profiles.map(p => p.refinement))
                                      .filter((id, i, all) => id.length > 0 && all.indexOf(id) === i)
    readonly property bool refines: inUse.length > 0
    readonly property var rating: Models.refinementRatings[primary]
    // The providers that have settings on this page, in the Switcher's order.
    readonly property var configurable: ["openai", "anthropic", "local", "endpoint"]
    // Which provider's settings show, when there are fallbacks.
    property string providerTab: primary

    // The page's fixed wording.
    readonly property var t: ({
        providerSettings: "Provider settings",
        signIn: "Sign-in",
        openAiSignIn: "Uses your ChatGPT or Codex sign-in. API keys only cover cleanup.",
        anthropicSignIn: "Uses your Claude Code sign-in, from its desktop app or /login in the claude CLI.",
        openAccounts: "Open Accounts",
        ultrafastPlans: "Ultrafast only works on ChatGPT Pro $500 and eligible Enterprise and Edu plans. Other plans run it at Standard.",
        ultrafastModels: "Ultrafast needs GPT-6 Astra or GPT-6.1 Sol. This model refines at Standard.",
        running: "running",
        notRunning: "not running",
        looking: "Looking…",
        lookAgain: "Look again",
        cleanupModel: "Cleanup model",
        pull: "Download through Ollama",
        contextCaption: "Context helps it spell names and match what you're writing. Nothing is kept after the dictation.",
        promptCaption: "The rules refinement follows. Most people never change them."
    })
    // Under the sample: who cleans it up, or that nothing does.
    readonly property string sampleCaption: {
        if (primary === "none") return "Pasted as spoken. Choose a provider below to clean it up."
        const model = ModelText.refinementModel(primary)
        return "Cleaned up by " + ModelText.provider(primary).label + (model ? " with " + model : "") + "."
    }
    readonly property string fallbacksCaption: "If " + ModelText.provider(primary).label + " fails or can't be reached, "
        + (fallbacks.length ? "Speecher tries these in order. Drag to reorder." : "the text is pasted as spoken.")

    Component.onCompleted: Models.checkComputer()

    // A new primary leaves the fallbacks, and None clears them.
    function choose(id) {
        Settings.set("refinementFallbacks", id === "none" ? [] : fallbacks.filter(f => f !== id))
        Settings.set("refinementProvider", id)
        providerTab = id
    }

    // What refinement does to a sentence, marked like a proof. With no
    // provider the marks fade and it reads as it will be pasted.
    Section {
        plain: true
        Plate {
            height: sample.implicitHeight + Theme.space5 * 2
            Column {
                id: sample
                x: Theme.space6
                y: Theme.space5
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
        SearchLanding { rowIds: ["refinementProvider"]; target: providerSection }
        ChoiceRows {
            recommendedText: Words.recommended
            options: States.withTones(ModelText.refinementChoices(Models.refinementProviders))
            currentId: page.primary
            onActivated: id => page.choose(id)
        }
        SchemaRow {
            rowId: "refinementRating"
            shown: !!page.rating
            help: page.rating && page.rating.ratedModel ? ModelText.ratedWithText(page.rating) : spec.help
            RatingBars { appearance: "wide"; bars: ModelText.bars(page.rating) || [] }
        }
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
                    SettingRow {
                        title: page.t.signIn
                        help: page.t.openAiSignIn
                        Button { text: page.t.openAccounts; size: "small"; onClicked: Navigation.go("accounts") }
                    }
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
                    SettingRow {
                        title: page.t.signIn
                        help: page.t.anthropicSignIn
                        Button { text: page.t.openAccounts; size: "small"; onClicked: Navigation.go("accounts") }
                    }
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

                Column {
                    id: local
                    readonly property var running: Models.runners.filter(r => r.running)
                    readonly property var chosen: Models.runners.find(r => r.id === Settings.values.localRunner)

                    SchemaRow {
                        rowId: "localRunnerDetect"
                        help: ModelText.runnersText
                        stacked: true
                        Row {
                            spacing: Theme.space5
                            Repeater {
                                model: Models.runners
                                Row {
                                    id: runner
                                    required property var modelData
                                    spacing: Theme.space2
                                    anchors.verticalCenter: parent.verticalCenter
                                    Txt { text: runner.modelData.label; variant: "label"; anchors.verticalCenter: parent.verticalCenter }
                                    // While a search runs, each says so with a spinner rather than greying out.
                                    StatusText {
                                        busy: Models.detectingRunners
                                        text: runner.modelData.running ? page.t.running : page.t.notRunning
                                        tone: runner.modelData.running ? "positive" : "neutral"
                                        indicator: runner.modelData.running ? "dot" : "none"
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                            Button {
                                text: Models.detectingRunners ? page.t.looking : page.t.lookAgain
                                iconName: "radar"
                                size: "small"
                                busy: Models.detectingRunners
                                enabled: !Models.detectingRunners
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: Models.detectRunners()
                            }
                        }
                    }
                    SchemaRow {
                        id: runnerRow
                        rowId: "localRunner"
                        Select {
                            options: local.running.map(r => ({ id: r.id, label: r.label }))
                                     .concat(local.chosen && !local.chosen.running ? [{ id: local.chosen.id, label: ModelText.stoppedRunner(local.chosen.label) }] : [])
                            currentId: Settings.values.localRunner || ""
                            placeholder: runnerRow.spec.placeholder
                            onChosen: id => Settings.set("localRunner", id)
                        }
                    }
                    SchemaRow {
                        id: runnerModelRow
                        rowId: "localRunnerModel"
                        SuggestField {
                            suggestionsText: Words.suggestions
                            text: Settings.values.localRunnerModel || ""
                            suggestions: (local.chosen ? local.chosen.models : []).map(m => ({ id: m, label: m }))
                            placeholderText: runnerModelRow.spec.placeholder
                            onCommitted: v => Settings.set("localRunnerModel", v)
                        }
                    }
                    // The cleanup model suggested here, which Ollama can
                    // download, and its download while it runs.
                    SettingRow {
                        shown: Settings.values.localRunner === "ollama" && (Models.offerPull || Models.pull.running || !Models.cleanupSuggestion.name)
                        title: page.t.cleanupModel
                        help: ModelText.pullText
                        Switcher {
                            width: implicitWidth
                            currentIndex: Models.pull.running ? 1 : 0
                            Button {
                                text: page.t.pull
                                iconName: "download"
                                size: "small"
                                enabled: Models.offerPull
                                onClicked: Models.pullCleanupModel()
                            }
                            Row {
                                spacing: Theme.space2
                                ProgressBar {
                                    width: 120
                                    value: Models.pull.totalBytes > 0 ? Models.pull.completedBytes / Models.pull.totalBytes : 0
                                    indeterminate: Models.pull.totalBytes <= 0
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                IconButton {
                                    size: 22
                                    iconName: "x"
                                    tooltip: Words.cancel
                                    onClicked: Models.cancelPull()
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }

                Column {
                    SchemaRow { rowId: "refinementEndpointServer" }
                    SchemaRow { rowId: "refinementEndpointFormat" }
                    SchemaRow { rowId: "refinementEndpointUrl" }
                    SchemaRow { rowId: "refinementEndpointApiKey" }
                    SchemaRow {
                        id: endpointModelRow
                        rowId: "refinementEndpointModel"
                        SuggestField {
                            suggestionsText: Words.suggestions
                            text: Settings.values.refinementEndpointModel || ""
                            suggestions: Models.refinementEndpoint.models.map(m => ({ id: m, label: m }))
                            placeholderText: endpointModelRow.spec.placeholder
                            onCommitted: v => Settings.set("refinementEndpointModel", v)
                        }
                    }
                    SchemaRow {
                        id: endpointTestRow
                        readonly property var check: ModelText.endpointCheck(Models.refinementEndpoint)
                        rowId: "refinementEndpointTest"
                        Row {
                            spacing: Theme.space3
                            StatusText {
                                text: endpointTestRow.check.text
                                busy: endpointTestRow.check.busy
                                tone: endpointTestRow.check.tone
                                indicator: endpointTestRow.check.dot ? "dot" : "none"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Button {
                                text: endpointTestRow.spec.actionLabel
                                iconName: "plug-zap"
                                size: "small"
                                enabled: !endpointTestRow.check.busy
                                onClicked: Models.testRefinementEndpoint()
                            }
                        }
                    }
                }
            }
        }
    }

    // Two short groups side by side. Fallbacks fade away with no provider,
    // and what refinement can see slides over to take the whole width.
    Reveal {
        open: page.refines

        Item {
            id: pair
            property real split: page.primary !== "none" ? 1 : 0
            Behavior on split { Ease { duration: Theme.slow } }
            readonly property bool paired: split === 1
            width: parent.width
            height: Math.max(fallbackSection.visible ? fallbackSection.height : 0, contextSection.height)

            Section {
                id: fallbackSection
                width: (pair.width - Theme.space4) / 2
                opacity: pair.split
                visible: opacity > 0
                minimumHeaderHeight: pair.paired ? contextSection.naturalHeaderHeight : 0
                minimumCardHeight: pair.paired ? contextSection.naturalCardHeight : 0
                title: Schema.row("refinementFallbacks").label
                caption: page.fallbacksCaption
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
                    emptyText: Schema.row("refinementFallbacks").emptyText
                    addLabel: Schema.row("refinementFallbacks").addLabel
                    addPlaceholder: Words.choose
                    onEdited: ids => Settings.set("refinementFallbacks", ids)
                }
            }

            Section {
                id: contextSection
                x: pair.split * (fallbackSection.width + Theme.space4)
                width: pair.width - x
                minimumHeaderHeight: pair.paired ? fallbackSection.naturalHeaderHeight : 0
                minimumCardHeight: pair.paired ? fallbackSection.naturalCardHeight : 0
                title: Schema.row("targetContextControl").section
                caption: page.t.contextCaption
                AccessibilityNotice { text: Schema.row("targetContextControl").disabledHelp }
                SchemaRow { rowId: "targetContextControl"; enabled: System.accessibility }
                SchemaRow {
                    rowId: "includeScreenshotContext"
                    enabled: page.inUse.some(id => !!ModelText.provider(id).readsScreenshots)
                    help: enabled ? spec.help : spec.disabledHelp
                }
            }
        }
    }

    Reveal {
        open: page.refines

        Section {
            title: Schema.row("customSystemPromptEnabled").section
            caption: page.t.promptCaption
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
