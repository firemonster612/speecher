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
    readonly property bool refines: options.refiner !== "none"

    spacing: Theme.sectionGap

    Section {
        title: TranscribeWords.audioFiles
        caption: TranscribeWords.filesCaption(Transcription.files, Library.pipelineCaption(view.options.service, view.options.refiner))

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

    // The service and the provider are chosen as on Speech to text and
    // Refinement, with the same rows.
    Section {
        title: TranscribeWords.transcription
        caption: TranscribeWords.serviceHelp
        ServiceCards {
            options: States.withTones(ModelText.fileSpeechChoices(Models.speechServices))
            currentId: view.options.service
            unratedText: ModelText.unrated
            onActivated: id => Transcription.setOption("service", id)
        }
        SettingRow {
            title: TranscribeWords.language
            help: TranscribeWords.languageHelp
            Select { options: Schema.row("spokenLanguage").options; currentId: view.options.language; onChosen: id => Transcription.setOption("language", id) }
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
        caption: TranscribeWords.refinerHelp
        ServiceCards {
            options: States.withTones(ModelText.refinementChoices(Models.refinementProviders))
            currentId: view.options.refiner
            unratedText: ModelText.unrated
            onActivated: id => Transcription.setOption("refiner", id)
        }
        SettingRow {
            enabled: view.refines
            title: TranscribeWords.model
            help: view.refines ? TranscribeWords.modelHint : TranscribeWords.needsRefiner
            clickable: view.refines
            trailingText: Transcription.refinerModel
            onClicked: Navigation.go("refinement")
        }
        SettingRow {
            enabled: view.refines
            title: TranscribeWords.cleanup
            help: view.refines ? TranscribeWords.cleanupHelp : TranscribeWords.needsRefiner
            CompactChoice { options: Library.levelOptions; currentId: view.options.cleanup; onChosen: id => Transcription.setOption("cleanup", id) }
        }
        SettingRow {
            title: TranscribeWords.profile
            help: TranscribeWords.profileHelp
            Select { options: Library.profiles.map(p => ({ id: p.id, label: p.name, icon: Library.profileIcon(p.id) })); currentId: view.options.profile; onChosen: id => Transcription.setOption("profile", id) }
        }
        SettingRow {
            enabled: view.refines
            title: TranscribeWords.tone
            help: view.refines ? TranscribeWords.toneHelp : TranscribeWords.needsRefiner
            Select { options: Library.toneOptions; currentId: view.options.tone; onChosen: id => Transcription.setOption("tone", id) }
        }
    }
}
