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
    readonly property var rating: Library.ratings[primary]
    // The providers that have settings on this page, in the Switcher's order.
    readonly property var configurable: ["openai", "anthropic", "local", "endpoint"]
    // Which provider's settings show, when there are fallbacks.
    property string providerTab: primary

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
                    segments: Library.refinementSample
                    mode: page.primary === "none" ? "spoken" : "marked"
                    variant: "lead"
                }
                MorphText {
                    text: Library.sampleCaption
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
            indicator: "check"
            rowHeight: 56
            // A provider that is chosen but not ready says so in the warning colour.
            options: Library.providerChoices
                     .map(o => Object.assign({}, o, { tone: o.id === page.primary && o.tone === "neutral" ? "warning" : "neutral" }))
            currentId: page.primary
            onActivated: id => page.choose(id)
        }
        SchemaRow {
            rowId: "refinementRating"
            shown: !!page.rating
            help: page.rating && page.rating.subject ? Library.ratedWith(page.rating.subject) : spec.help
            RatingBars { appearance: "wide"; bars: page.rating ? page.rating.bars : [] }
        }
    }

    // Settings for every provider a dictation may use: the chain's, and
    // those profiles pick for themselves.
    Reveal {
        open: page.inUse.length > 0

        Section {
            id: providerSettings
            readonly property string shown: page.inUse.includes(page.providerTab) ? page.providerTab : page.inUse[0] || ""
            title: page.inUse.length > 1 ? Library.refinementText.providerSettings : Library.provider(shown).label
            // With more than one, a switch between them.
            actions: [
                SegmentedControl {
                    id: providerTabs
                    objectName: "providerTabs"
                    options: page.inUse.map(id => ({ id: id, label: Library.provider(id).label }))
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
                        title: Library.refinementText.signIn
                        help: Library.provider("openai").signIn
                        Button { text: Library.refinementText.openAccounts; size: "small"; onClicked: Navigation.go("accounts") }
                    }
                    SchemaRow {
                        rowId: "openAiModel"
                        SuggestField {
                            suggestionsText: Words.suggestions
                            text: Settings.values.openAiModel || ""
                            suggestions: Library.openAiModels
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
                            text: Library.ultrafastModels.includes(Settings.values.openAiModel)
                                  ? Library.refinementText.ultrafastPlans
                                  : Library.refinementText.ultrafastModels
                        }
                    }
                }

                Column {
                    SettingRow {
                        title: Library.refinementText.signIn
                        help: Library.provider("anthropic").signIn
                        Button { text: Library.refinementText.openAccounts; size: "small"; onClicked: Navigation.go("accounts") }
                    }
                    SchemaRow {
                        rowId: "anthropicModel"
                        SuggestField {
                            suggestionsText: Words.suggestions
                            text: Settings.values.anthropicModel || ""
                            suggestions: Library.anthropicModels
                            onCommitted: v => Settings.set("anthropicModel", v)
                        }
                    }
                    SchemaRow { rowId: "anthropicEffort" }
                    SchemaRow { rowId: "anthropicFastMode" }
                }

                Column {
                    id: local
                    readonly property var running: Library.runners.filter(r => r.running)
                    readonly property var chosen: Library.runners.find(r => r.id === Settings.values.localRunner)

                    SchemaRow {
                        rowId: "localRunnerDetect"
                        help: Library.runnersText
                        stacked: true
                        Row {
                            spacing: Theme.space5
                            Repeater {
                                model: Library.runners
                                Row {
                                    id: runner
                                    required property var modelData
                                    spacing: Theme.space2
                                    opacity: Library.detectingRunners ? 0.5 : 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    Behavior on opacity { Ease {} }
                                    Txt { text: runner.modelData.label; variant: "label"; anchors.verticalCenter: parent.verticalCenter }
                                    StatusText {
                                        text: runner.modelData.running ? Library.refinementText.running : Library.refinementText.notRunning
                                        tone: runner.modelData.running ? "positive" : "neutral"
                                        indicator: runner.modelData.running ? "dot" : "none"
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }
                            }
                            Button {
                                text: Library.detectingRunners ? Library.refinementText.looking : Library.refinementText.lookAgain
                                iconName: "radar"
                                size: "small"
                                busy: Library.detectingRunners
                                enabled: !Library.detectingRunners
                                anchors.verticalCenter: parent.verticalCenter
                                onClicked: Library.detectRunners()
                            }
                        }
                    }
                    SchemaRow {
                        id: runnerRow
                        rowId: "localRunner"
                        Select {
                            options: local.running.map(r => ({ id: r.id, label: r.label }))
                                     .concat(local.chosen && !local.chosen.running ? [{ id: local.chosen.id, label: Library.stoppedRunnerLabel(local.chosen.label) }] : [])
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
                            suggestions: Library.endpointModels.map(m => ({ id: m, label: m }))
                            placeholderText: endpointModelRow.spec.placeholder
                            onCommitted: v => Settings.set("refinementEndpointModel", v)
                        }
                    }
                    SchemaRow {
                        id: endpointTestRow
                        rowId: "refinementEndpointTest"
                        Row {
                            spacing: Theme.space3
                            StatusText {
                                text: Library.testingEndpoint ? Library.refinementText.connecting : Library.endpointStatus || Library.refinementText.notTested
                                busy: Library.testingEndpoint
                                tone: Library.endpointStatus ? "positive" : "neutral"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Button {
                                text: endpointTestRow.spec.actionLabel
                                iconName: "plug-zap"
                                size: "small"
                                enabled: !Library.testingEndpoint
                                onClicked: Library.testEndpoint()
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
                caption: Library.fallbacksCaption
                SearchLanding { rowIds: ["refinementFallbacks"]; target: fallbackSection }
                FallbackList {
                    moveUpText: Words.moveUp
                    moveDownText: Words.moveDown
                    removeText: Words.remove
                    ids: page.fallbacks
                    options: Library.refinementProviders.filter(p => p.id !== page.primary)
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
                caption: Library.refinementText.contextCaption
                AccessibilityNotice { text: Schema.row("targetContextControl").disabledHelp }
                SchemaRow { rowId: "targetContextControl"; enabled: System.accessibility }
                SchemaRow {
                    rowId: "includeScreenshotContext"
                    enabled: page.inUse.some(id => Library.provider(id).readsScreenshots)
                    help: enabled ? spec.help : spec.disabledHelp
                }
            }
        }
    }

    Reveal {
        open: page.refines

        Section {
            title: Schema.row("customSystemPromptEnabled").section
            caption: Library.refinementText.promptCaption
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
                        text: Settings.values.customSystemPrompt || Library.builtInSystemPrompt
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
                        enabled: !!Settings.values.customSystemPrompt && Settings.values.customSystemPrompt !== Library.builtInSystemPrompt
                        onClicked: Settings.set("customSystemPrompt", "")
                    }
                }
            }
        }
    }
}
