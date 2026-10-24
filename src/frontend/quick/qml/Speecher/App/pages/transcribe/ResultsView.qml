pragma ComponentBehavior: Bound
import QtQuick
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
            onPicked: id => Transcription.exportAll(id, TranscribeWords.exportAllDialog)
        }
    ]

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
            required property string key
            width: ListView.view.width
            result: results.entry(key)
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
