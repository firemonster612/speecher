pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Step one: the files to transcribe with the drop zone under them, then
// how they are heard, where the transcripts go, and how they are cleaned up.
Column {
    id: view

    readonly property var options: Transcription.options
    readonly property var planned: Transcription.plan
    readonly property bool refines: planned.refiner !== "none"
    readonly property var service: ModelText.service(planned.service)
    readonly property var refiner: ModelText.provider(planned.refiner)
    // What the model fields suggest, as Speech to text and Refinement do:
    // each provider's known models, the Local Runner's and the endpoints'
    // as they were listed.
    readonly property var refinerModels: {
        const named = names => names.map(m => ({ id: m, label: m }))
        const runner = Models.runners.find(r => r.id === Settings.values.localRunner)
        return planned.refiner === "openai" || planned.refiner === "anthropic" ? ModelText.suggestedModels(planned.refiner)
             : planned.refiner === "local" ? named(runner ? runner.models : [])
             : planned.refiner === "endpoint" ? named(Models.refinementEndpoint.models) : []
    }

    spacing: Theme.sectionGap

    Section {
        title: TranscribeWords.audioFiles
        caption: TranscribeWords.filesCaption(Transcription.files, Library.pipelineCaption(view.planned.service, view.planned.refiner, Transcription.plan.refinerModel))

        RowList {
            id: files
            values: Transcription.files
            keyOf: f => f.path
            delegate: ListRow {
                id: fileRow
                required property string key
                required property int index
                readonly property var file: files.entry(key)
                width: ListView.view.width
                first: index === 0 && files.first
                iconName: file.kind === "video" ? "file-video" : "file-audio"
                title: file.name
                detail: TranscribeWords.fileDetail(file)
                trailing: IconButton { iconName: "x"; size: 28; tooltip: Words.remove; onClicked: Transcription.removeFile(fileRow.key) }
            }
        }
        // Inset on every side while it is the whole card, below the list after.
        Item {
            property real gap: files.count ? 0 : Theme.space4
            width: parent.width
            height: drop.height + Theme.space4 + gap
            Behavior on gap { Ease { duration: Theme.slow } }
            DropZone {
                id: drop
                x: Theme.rowInset
                y: parent.gap
                width: parent.width - 2 * Theme.rowInset
                compact: files.count > 0
                onChooseRequested: fileChooser.open()
            }
        }
        // Why the batch did not start.
        Reveal {
            open: Transcription.problem.length > 0
            Notice { inset: Theme.rowInset; tone: "negative"; text: Transcription.problem }
        }
    }

    // The Writing Profile first, on its own card, since choosing one sets
    // the services, cleanup and tone in both sections below; each can still
    // be changed.
    Section {
        SettingRow {
            title: TranscribeWords.profile
            help: TranscribeWords.profileHelp
            Select { options: Library.profiles.map(p => ({ id: p.id, label: p.name, icon: Library.profileIcon(p.id) })); currentId: view.planned.profile; onChosen: id => Transcription.setOption("profile", id) }
        }
    }

    // The service and the provider start as the profile has them, so each
    // folds to one row; opened, it is chosen as on Speech to text and
    // Refinement, with the same cards.
    Section {
        title: TranscribeWords.transcription
        Disclosure {
            title: TranscribeWords.service
            help: TranscribeWords.serviceHelp
            trailingText: view.service.label
            ServiceCards {
                options: States.withTones(ModelText.fileSpeechChoices(Models.speechServices))
                currentId: view.planned.service
                unratedText: ModelText.unrated
                onActivated: id => Transcription.setOption("service", id)
                onHelpLinkActivated: (id, at) => serviceLanguages.show(at, ModelText.service(id).languages)
            }
        }
        // What the chosen service still needs, with its Fix; these files
        // wait here meanwhile. A downloaded Local Model is picked for these
        // files below, so Local Model needs nothing more.
        ServiceStatus {
            role: "speech"
            serviceId: view.planned.service === "local" && ModelText.downloadedModels.length > 0 ? "" : view.planned.service
        }
        LanguageList { id: serviceLanguages }
        // The model, for these files, of a service that has one to choose.
        // Without a downloaded Local Model the service's card says what to do.
        SettingRow {
            shown: view.planned.service === "local" && ModelText.downloadedModels.length > 0
            title: TranscribeWords.model
            help: TranscribeWords.speechModelHelp
            Select {
                options: ModelText.downloadedModels
                currentId: Transcription.plan.speechModel
                placeholder: TranscribeWords.chooseModel
                onChosen: id => Transcription.setOption("speechModel", id)
            }
        }
        SettingRow {
            shown: view.planned.service === "endpoint"
            title: TranscribeWords.model
            help: TranscribeWords.speechModelHelp
            SuggestField {
                suggestionsText: Words.suggestions
                text: Transcription.plan.speechModel
                suggestions: Models.speechEndpoint.models.map(m => ({ id: m, label: m }))
                onCommitted: v => Transcription.setOption("speechModel", v)
            }
        }
        SettingRow {
            title: TranscribeWords.language
            help: TranscribeWords.languageHelp
            LanguageSelect {
                placeholder: Words.choose; searchText: Words.searchLanguages; clearText: Words.clear; emptyText: Words.noLanguage
                options: Schema.row("spokenLanguage").options; currentId: view.planned.language
                onChosen: id => Transcription.setOption("language", id)
            }
        }
        SettingRow {
            title: TranscribeWords.vocabulary
            help: TranscribeWords.vocabularyHelp
            Switch { checked: view.options.vocabulary; onToggled: Transcription.setOption("vocabulary", checked) }
        }
    }

    Section {
        title: TranscribeWords.output
        SettingRow {
            title: TranscribeWords.saveTranscripts
            help: TranscribeWords.destinationHint(view.options.destination)
            Select {
                options: TranscribeWords.destinations
                currentId: view.options.destination
                onChosen: id => {
                    Transcription.setOption("destination", id)
                    if (id === "folder" && !view.options.folder) folderChooser.open()
                }
            }
        }
        SettingRow {
            shown: view.options.destination === "folder"
            title: TranscribeWords.folder
            help: view.options.folder
            Button { text: TranscribeWords.changeFolder; size: "small"; onClicked: folderChooser.open() }
        }
    }

    // The desktop's own choosers, attached to the window.
    FileDialog {
        id: fileChooser
        title: TranscribeWords.filesDialog
        fileMode: FileDialog.OpenFiles
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        nameFilters: TranscribeWords.fileFilters(Transcription.extensions)
        onAccepted: Transcription.addUrls(fileChooser.selectedFiles)
    }
    FolderDialog {
        id: folderChooser
        title: TranscribeWords.folderDialog
        currentFolder: view.options.folderUrl || StandardPaths.writableLocation(StandardPaths.HomeLocation)
        onAccepted: Transcription.setFolder(folderChooser.selectedFolder)
        onRejected: Transcription.setFolder("")
    }

    Section {
        title: TranscribeWords.refinement
        Disclosure {
            title: TranscribeWords.service
            help: TranscribeWords.refinerHelp
            trailingText: view.refiner.label
            ServiceCards {
                options: States.withTones(ModelText.refinementChoices(Models.refinementProviders))
                currentId: view.planned.refiner
                unratedText: ModelText.unrated
                onActivated: id => Transcription.setOption("refiner", id)
            }
        }
        // What the chosen service still needs, with its Fix.
        ServiceStatus { role: "refinement"; serviceId: view.planned.refiner }
        // The refiner's model, for these files.
        SettingRow {
            enabled: view.refines
            title: TranscribeWords.model
            help: view.refines ? TranscribeWords.refinerModelHelp : TranscribeWords.needsRefiner
            SuggestField {
                suggestionsText: Words.suggestions
                text: Transcription.plan.refinerModel
                suggestions: view.refinerModels
                onCommitted: v => Transcription.setOption("refinerModel", v)
            }
        }
        SettingRow {
            enabled: view.refines
            title: TranscribeWords.cleanup
            help: view.refines ? TranscribeWords.cleanupHelp : TranscribeWords.needsRefiner
            CompactChoice { options: Library.levelOptions; currentId: view.planned.cleanup; onChosen: id => Transcription.setOption("cleanup", id) }
        }
        SettingRow {
            enabled: view.refines
            title: TranscribeWords.tone
            help: view.refines ? TranscribeWords.toneHelp : TranscribeWords.needsRefiner
            Select { options: Library.toneOptions; currentId: view.planned.tone; onChosen: id => Transcription.setOption("tone", id) }
        }
    }
}
