pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "../text"
import "../transcripts"

// Step one: the files to transcribe with the drop zone under them, what
// they run with (the Writing Profile and what these files change of it) and
// where the transcripts go, in the same shape as Recordings.
Column {
    id: view

    // What the batch changes, "" where it is the same as the profile or the
    // settings, what it runs with, and what each would be without its change.
    readonly property var options: Transcription.options
    readonly property var planned: Transcription.plan
    readonly property var inherited: Transcription.inherited
    readonly property string profileName: Library.profileName(planned.profile)
    readonly property int changes: ["service", "refiner", "cleanup", "tone", "language"].filter(id => !!options[id]).length
                                   + (options.vocabulary ? 0 : 1)
    // Where a value the batch leaves alone comes from: the profile when it
    // sets it, otherwise the page that does.
    function place(source, pageId) { return (source || "").startsWith("profile:") ? profileName : Schema.page(pageId).title }
    function languageName(id) { return (Schema.row("spokenLanguage").options.find(o => o.id === id) || { label: id }).label }
    // The editor's edits, by the batch's option ids. A new service drops its
    // model on its own.
    function edit(patch) {
        const ids = { cleanup: "cleanup", tone: "tone", speech: "service", refinement: "refiner" }
        for (const key in ids) if (key in patch) Transcription.setOption(ids[key], patch[key])
        if ("speechModel" in patch && !("speech" in patch)) Transcription.setOption("speechModel", patch.speechModel)
        if ("refinementModel" in patch && !("refinement" in patch)) Transcription.setOption("refinerModel", patch.refinementModel)
    }

    spacing: Theme.sectionGap

    Section {
        title: TranscribeWords.audioFiles
        caption: TranscribeWords.filesCaption(Transcription.files, Library.pipelineCaption(view.planned.service, view.planned.refiner, view.planned.refinerModel))

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

    // The Writing Profile, then what these files change of it: the profile
    // editor in run mode, each control starting "Same as …", and the
    // Spoken language and vocabulary, which only a run changes here.
    RunWith {
        profileId: view.options.profile
        inheritedProfile: view.inherited.profile
        profileHelp: TranscribeWords.profileHelp
        changesTitle: TranscribeWords.changes
        changesHelp: TranscribeWords.changesHelp(view.profileName)
        changes: view.changes
        // A downloaded Local Model is picked in the editor, so Local Model
        // needs nothing more.
        speechService: view.planned.service === "local" && ModelText.downloadedModels.length > 0 ? "" : view.planned.service
        refinementService: view.planned.refiner === "none" ? "" : view.planned.refiner
        onProfileChosen: id => Transcription.setOption("profile", id)

        Item {
            width: parent.width
            height: runEditor.implicitHeight + Theme.space4 * 2
            ProfileEditor {
                id: runEditor
                objectName: "runEditor"
                x: Theme.rowInset
                y: Theme.space4
                width: parent.width - 2 * Theme.rowInset
                runMode: true
                servicesTitle: TranscribeWords.services
                profile: ({ cleanup: view.options.cleanup, tone: view.options.tone,
                            speech: view.options.service, speechModel: view.options.speechModel,
                            refinement: view.options.refiner, refinementModel: view.options.refinerModel })
                inherited: ({ profileName: view.profileName,
                              cleanup: view.inherited.cleanup, tone: view.inherited.tone,
                              outputLanguage: view.inherited.outputLanguage,
                              speech: view.inherited.service, speechModel: view.inherited.speechModel,
                              speechPlace: view.place(view.inherited.sources.speech, "speech"),
                              refinement: view.inherited.refiner, refinementModel: view.inherited.refinerModel,
                              refinementPlace: view.place(view.inherited.sources.refinement, "refinement") })
                onEdited: patch => view.edit(patch)
            }
        }
        SettingRow {
            title: TranscribeWords.language
            help: TranscribeWords.languageHelp
            LanguageSelect {
                placeholder: Words.choose; searchText: Words.searchLanguages; clearText: Words.clear; emptyText: Words.noLanguage
                options: [{ id: "", label: Words.sameAs(Schema.page("speech").title, view.languageName(view.inherited.language)), icon: "link" }]
                         .concat(Schema.row("spokenLanguage").options)
                currentId: view.options.language
                shownId: view.inherited.language
                onChosen: id => Transcription.setOption("language", id)
            }
        }
        SettingRow {
            title: TranscribeWords.vocabulary
            help: TranscribeWords.vocabularyHelp
            Switch { checked: view.options.vocabulary; onToggled: Transcription.setOption("vocabulary", checked) }
        }
    }

    SaveTranscripts {
        id: output
        destinations: TranscribeWords.destinations
        destination: view.options.destination
        destinationHelp: TranscribeWords.destinationHint(view.options.destination)
        folder: view.options.folder || ""
        folderUrl: view.options.folderUrl || ""
        onDestinationChosen: id => {
            Transcription.setOption("destination", id)
            if (id === "folder" && !view.options.folder) output.chooseFolder()
        }
        onFolderChosen: folder => Transcription.setFolder(folder)
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
}
