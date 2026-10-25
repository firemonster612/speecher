pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The Local Runner's rows, on the Refinement page and setup's Refinement
// step: the runners running here, the one chosen and its cleanup model, and
// the cleanup model Ollama can download, with its download while it runs.
// They look for runners when they load, and each row saves as it changes.
Column {
    id: local
    width: parent ? parent.width : implicitWidth

    readonly property var running: Models.runners.filter(r => r.running)
    readonly property var chosen: Models.runners.find(r => r.id === Settings.values.localRunner)

    // The rows' fixed wording.
    readonly property var t: ({
        running: "running",
        notRunning: "not running",
        looking: "Looking…",
        lookAgain: "Look again",
        cleanupModel: "Cleanup model",
        pull: "Download through Ollama"
    })

    Component.onCompleted: Models.detectRunners()

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
                        text: runner.modelData.running ? local.t.running : local.t.notRunning
                        tone: runner.modelData.running ? "positive" : "neutral"
                        indicator: runner.modelData.running ? "dot" : "none"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
            Button {
                text: Models.detectingRunners ? local.t.looking : local.t.lookAgain
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
    // The cleanup model suggested here, which Ollama can download, and its
    // download while it runs.
    SettingRow {
        shown: Settings.values.localRunner === "ollama" && (Models.offerPull || Models.pull.running || !Models.cleanupSuggestion.name)
        title: local.t.cleanupModel
        help: ModelText.pullText
        Switcher {
            width: implicitWidth
            currentIndex: Models.pull.running ? 1 : 0
            Button {
                text: local.t.pull
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
