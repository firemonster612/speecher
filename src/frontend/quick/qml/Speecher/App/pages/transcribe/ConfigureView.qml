pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App

// Step one: the files to transcribe with the drop zone under them, then
// how they are heard, where the transcripts go, and how they are cleaned up.
Column {
    id: view

    readonly property var t: Transcription.text
    readonly property var options: Transcription.options

    spacing: Theme.sectionGap

    Section {
        title: view.t.audioFiles
        caption: Transcription.filesCaption

        RowList {
            id: files
            values: Transcription.files
            keyOf: f => f.name
            delegate: ListRow {
                id: fileRow
                required property string key
                required property int index
                readonly property var file: files.entry(key)
                width: ListView.view.width
                first: index === 0 && files.first
                iconName: file.kind === "video" ? "file-video" : "file-audio"
                title: key
                detail: Transcription.fileDetail(file)
                trailing: IconButton { iconName: "x"; size: 28; tooltip: view.t.remove; onClicked: Transcription.removeFile(fileRow.index) }
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
                onChooseRequested: Transcription.addFiles()
            }
        }
    }

    // Two short groups side by side, then the refinement rows.
    Row {
        width: parent.width
        spacing: Theme.space4
        Section {
            id: hearing
            width: (parent.width - parent.spacing) / 2
            minimumHeaderHeight: output.naturalHeaderHeight
            minimumCardHeight: output.naturalCardHeight
            title: view.t.transcription
            SettingRow {
                title: view.t.service
                help: view.t.serviceHelp
                Select { options: Transcription.services; currentId: view.options.service; implicitWidth: 170; onChosen: id => Transcription.setOption("service", id) }
            }
            SettingRow {
                title: view.t.vocabulary
                help: view.t.vocabularyHelp
                Switch { checked: view.options.vocabulary; onToggled: Transcription.setOption("vocabulary", checked) }
            }
        }
        Section {
            id: output
            width: (parent.width - parent.spacing) / 2
            minimumHeaderHeight: hearing.naturalHeaderHeight
            minimumCardHeight: hearing.naturalCardHeight
            title: view.t.output
            SettingRow {
                title: view.t.saveTranscripts
                help: Transcription.destinationHint
                stacked: true
                Select { options: Transcription.destinations; currentId: view.options.destination; width: parent.width; onChosen: id => Transcription.setOption("destination", id) }
            }
            SettingRow {
                shown: view.options.destination === "folder"
                title: view.t.folder
                help: view.options.folder
                Button { text: view.t.changeFolder; size: "small"; onClicked: Transcription.chooseFolder() }
            }
        }
    }

    Section {
        title: view.t.refinement
        SettingRow {
            title: view.t.refiner
            help: view.t.refinerHelp
            Select { options: Transcription.refiners; currentId: view.options.refiner; onChosen: id => Transcription.setOption("refiner", id) }
        }
        SettingRow {
            enabled: Transcription.refines
            title: view.t.model
            help: Transcription.refines ? view.t.modelHint : view.t.needsRefiner
            clickable: Transcription.refines
            trailingText: Transcription.refinerModel
            onClicked: Navigation.go("refinement")
        }
        SettingRow {
            enabled: Transcription.refines
            title: view.t.cleanup
            help: Transcription.refines ? view.t.cleanupHelp : view.t.needsRefiner
            Select { options: Transcription.cleanupLevels; currentId: view.options.cleanup; onChosen: id => Transcription.setOption("cleanup", id) }
        }
        SettingRow {
            enabled: Transcription.refines
            title: view.t.profile
            help: Transcription.refines ? view.t.profileHelp : view.t.needsRefiner
            Select { options: Transcription.profiles; currentId: view.options.profile; onChosen: id => Transcription.setOption("profile", id) }
        }
        SettingRow {
            enabled: Transcription.refines
            title: view.t.tone
            help: Transcription.refines ? view.t.toneHelp : view.t.needsRefiner
            Select { options: Transcription.tones; currentId: view.options.tone; onChosen: id => Transcription.setOption("tone", id) }
        }
    }
}
