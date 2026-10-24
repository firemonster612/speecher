pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Step three: a card per file with its transcript, refined or raw, and
// copy and export for one file or all of them.
Section {
    id: view

    // Subtitles only while some transcript has the timings for them.
    readonly property var formats: Transcription.results.some(r => r.timings) ? TranscribeWords.exportFormats
                                                                              : TranscribeWords.exportFormats.slice(0, 1)

    plain: true
    title: TranscribeWords.resultsTitle(Transcription.results)
    caption: TranscribeWords.batchSummary(Transcription.batch, Transcription.results)
    actions: [
        SegmentedControl {
            objectName: "transcriptView"
            options: [{ id: "refined", label: TranscribeWords.refined, icon: "pencil-line" }, { id: "raw", label: TranscribeWords.raw, icon: "audio-lines" }]
            currentId: Transcription.showRaw ? "raw" : "refined"
            onActivated: id => Transcription.showRaw = id === "raw"
            // A batch that didn't refine has only the raw transcript.
            enabled: Transcription.batch.refines
            opacity: enabled ? 1 : 0
            Behavior on opacity { Ease {} }
        },
        CopyButton {
            text: TranscribeWords.copyAll
            copiedText: Words.copied
            onCopied: Transcription.copyAll()
        },
        MenuButton {
            id: exportAll
            label: TranscribeWords.exportAll
            leadingIcon: "download"
            items: view.formats
            onPicked: id => {
                exportFolder.format = id
                exportFolder.open()
            }
        }
    ]

    // The desktop's own choosers, attached to the window.
    FileDialog {
        id: exportFile
        property int index: -1
        property string format
        function ask(index, format) {
            exportFile.index = index
            exportFile.format = format
            exportFile.selectedFile = Transcription.exportFile(index, format)
            exportFile.nameFilters = [TranscribeWords.exportFilter(format, exportFile.selectedFile)]
            exportFile.open()
        }
        title: TranscribeWords.exportDialog
        // The desktop's save dialog asks before saving over a file, as it
        // does for every app; the suggested name can be the transcript saved
        // beside the recording.
        fileMode: FileDialog.SaveFile
        onAccepted: Transcription.exportResult(exportFile.index, exportFile.format, exportFile.selectedFile)
    }
    FolderDialog {
        id: exportFolder
        property string format
        title: TranscribeWords.exportAllDialog
        currentFolder: Transcription.options.folderUrl || StandardPaths.writableLocation(StandardPaths.HomeLocation)
        onAccepted: Transcription.exportAll(exportFolder.format, exportFolder.selectedFolder)
    }

    Connections {
        target: Transcription
        function onExported(index) {
            if (index < 0) exportAll.confirm(TranscribeWords.saved)
        }
    }

    RowList {
        id: results
        values: Transcription.results
        keyOf: r => r.path
        spacing: Theme.space3
        delegate: ResultCard {
            id: card
            required property string key
            width: ListView.view.width
            result: results.entry(key)
            onExportRequested: format => exportFile.ask(card.index, format)
        }
    }
    // Why the last export or retry did not happen.
    Reveal {
        open: Transcription.problem.length > 0
        Notice { tone: "negative"; text: Transcription.problem }
    }
    Txt {
        x: 2
        width: parent.width - 4
        topPadding: Theme.space3
        text: TranscribeWords.subtitlesNote(Transcription.batch, Transcription.results)
        variant: "small"
        tone: "tertiary"
        wrapMode: Text.WordWrap
    }
}
