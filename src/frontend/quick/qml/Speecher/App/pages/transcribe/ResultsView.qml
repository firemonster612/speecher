pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Step three: a card per file with its transcript, refined or raw, and
// copy and export for one file or all of them.
Section {
    id: view

    readonly property var t: Transcription.text

    plain: true
    title: Transcription.resultsTitle
    caption: Transcription.batchSummary
    actions: [
        SegmentedControl {
            objectName: "transcriptView"
            options: [{ id: "refined", label: view.t.refined, icon: "pencil-line" }, { id: "raw", label: view.t.raw, icon: "audio-lines" }]
            currentId: Transcription.showRaw ? "raw" : "refined"
            onActivated: id => Transcription.showRaw = id === "raw"
            // A batch that didn't refine has only the raw transcript.
            enabled: Transcription.refined
            opacity: enabled ? 1 : 0
            Behavior on opacity { Ease {} }
        },
        CopyButton {
            text: view.t.copyAll
            copiedText: Words.copied
            onCopied: Transcription.copyAll()
        },
        MenuButton {
            label: view.t.exportAll
            leadingIcon: "download"
            items: Transcription.exportFormats
            onPicked: id => confirm(Transcription.exportAll(id))
        }
    ]

    RowList {
        id: results
        values: Transcription.results
        keyOf: r => r.name
        spacing: Theme.space3
        delegate: ResultCard {
            required property string key
            width: ListView.view.width
            result: results.entry(key)
        }
    }
    Txt {
        x: 2
        width: parent.width - 4
        topPadding: Theme.space3
        text: Transcription.subtitlesNote
        variant: "small"
        tone: "tertiary"
        wrapMode: Text.WordWrap
    }
}
