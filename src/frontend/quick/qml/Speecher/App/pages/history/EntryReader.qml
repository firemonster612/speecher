pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "HistoryWords.js" as T

// A file or a Recording opened from History, in the list's place: its name
// and where and when it ran, one row of actions, what went wrong, then its
// whole transcript, a Recording's lines with their times and speakers, a
// file's paragraphs. Both have Play with Stop and the position while its
// audio plays, Copy, Open transcript, Show in folder and Delete; a
// Recording adds Export, a file Refined or As spoken, Refine again and
// Transcribe again, or Locate… once its audio has moved. Delete asks first
// for a Recording, which may take its files and has no Undo; a file's goes
// at once with Undo (deleteRequested, which the page does). History's
// search carries over into Find, and a matching line it was opened at shows
// first. A Recording's files that won't go keep it, saying which, with Try
// again and Remove from History only.
Column {
    id: root

    property real entryId: 0
    // The line to show first, by its time; -1 for the start.
    property real startAt: -1
    property string find
    // Find in its lines.
    readonly property alias findField: lines.findField
    property bool askDelete: false
    property bool alsoDeleteFiles: false
    // The files the last delete couldn't take.
    property var filesStayed: []
    // A file's As spoken version shows in place of the Refined one.
    property bool spoken: false
    // Read again as History changes, so a recording that finishes while
    // open shows all of it.
    readonly property var entry: { void History.kept; return entryId > 0 ? History.entry(entryId) : ({}) }
    readonly property bool recording: entry.kind === "recording"
    readonly property bool playing: entryId > 0 && History.playing.id === entryId
    readonly property var notes: recording ? T.endingNotes(entry.ending) : []
    readonly property string home: StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", "")
    // A file's text a paragraph a line, as the reader shows lines.
    readonly property var paragraphs: ((spoken ? entry.raw : entry.text) || "").split(/\n+/).filter(p => p.trim().length > 0)
                                      .map(p => ({ atMs: 0, speaker: "", text: p }))
    signal closed()
    signal deleteRequested()

    function remove(withFiles) {
        filesStayed = History.removeRecording(entryId, withFiles)
        if (filesStayed.length === 0) closed()
    }
    function requestDelete() {
        if (recording) askDelete = true
        else deleteRequested()
    }
    function transcribeAgain(url) {
        Transcription.addUrls([url])
        Navigation.follow("transcribe", "")
    }

    onEntryIdChanged: {
        askDelete = false
        filesStayed = []
        spoken = false
        alsoDeleteFiles = !!entry.inRecordingsFolder
        if (startAt >= 0) Qt.callLater(() => lines.showAt(startAt))
    }

    spacing: Theme.space3
    width: parent ? parent.width : implicitWidth

    TextLink { text: T.text.back; iconName: "arrow-left"; onActivated: root.closed() }

    Section {
        width: parent.width
        title: root.entryId <= 0 ? "" : root.recording ? T.recordingTitle(root.entry) : T.fileName(root.entry)
        caption: root.entryId > 0 ? T.readerDetail(root.entry, Insights.today, root.home) : ""

        Item {
            width: parent.width
            implicitHeight: actions.implicitHeight + Theme.space3 * 2
            Flow {
                id: actions
                x: Theme.rowInset
                y: Theme.space3
                width: parent.width - 2 * Theme.rowInset
                spacing: Theme.space2
                SegmentedControl {
                    visible: !root.recording && !!root.entry.hasRaw
                    options: [{ id: "refined", label: T.text.refined }, { id: "spoken", label: T.text.spoken }]
                    currentId: root.spoken ? "spoken" : "refined"
                    onActivated: id => root.spoken = id === "spoken"
                }
                Button {
                    objectName: "readerPlay"
                    visible: !!root.entry.hasAudio
                    iconName: root.playing ? "square" : "play"
                    text: root.playing ? Words.clock(History.playing.positionMs || 0) + " / " + Words.clock(History.playing.durationMs || root.entry.durationMs)
                                       : Words.clock(root.entry.durationMs)
                    tooltip: root.playing ? T.text.stop : T.text.play
                    size: "small"
                    onClicked: root.playing ? History.stopPlaying() : History.play(root.entryId)
                }
                Button {
                    objectName: "readerCopy"
                    text: T.text.copy
                    iconName: "copy"
                    size: "small"
                    onClicked: {
                        History.copy(root.entryId, root.spoken)
                        Toaster.show(T.text.copied, { icon: "copy" })
                    }
                }
                MenuButton {
                    visible: root.recording
                    label: T.text.export_
                    leadingIcon: "file-down"
                    size: "small"
                    items: T.text.exportFormats
                    onPicked: id => exportFile.ask(id)
                }
                MenuButton {
                    objectName: "refineAgain"
                    visible: !root.recording && !!root.entry.raw
                    label: refine.refining ? T.text.refining : T.text.refineAgain
                    size: "small"
                    enabled: !refine.refining
                    items: refine.profiles
                    onPicked: id => refine.start(id)
                }
                Button {
                    objectName: "transcribeAgain"
                    visible: !root.recording && !root.entry.sourceMissing
                    text: T.text.transcribeAgain
                    iconName: "audio-lines"
                    size: "small"
                    onClicked: root.transcribeAgain(root.entry.sourceUrl)
                }
                Button {
                    visible: !!root.entry.transcriptPath
                    text: T.text.openTranscript
                    iconName: "external-link"
                    size: "small"
                    enabled: !root.entry.fileMissing
                    onClicked: History.openFile(root.entryId)
                }
                Button { text: T.text.showInFolder; iconName: "folder-open"; size: "small"; onClicked: History.showInFolder(root.entryId) }
                Button {
                    objectName: "deleteEntry"
                    text: T.text.delete_
                    iconName: "trash-2"
                    size: "small"
                    variant: "danger"
                    onClicked: root.requestDelete()
                }
            }
        }
        Reveal {
            open: !root.recording && root.entryId > 0
            Txt {
                x: Theme.rowInset
                width: parent.width - 2 * Theme.rowInset
                bottomPadding: Theme.space3
                text: T.fileStays(root.entry)
                variant: "small"
                tone: "secondary"
                wrapMode: Text.WordWrap
            }
        }
        Reveal {
            open: !root.recording && !!root.entry.sourceMissing
            Notice {
                objectName: "sourceMissing"
                text: T.sourceMissing(root.entry, root.home)
                inset: Theme.rowInset
                actionText: T.text.locate
                onAction: locate.open()
            }
        }
        Reveal {
            open: !!root.entry.fileMissing
            Notice { text: T.text.fileMissing; inset: Theme.rowInset }
        }
        Reveal {
            open: !!root.entry.fileDiverges
            Notice { text: T.text.fileDiverges; inset: Theme.rowInset }
        }
        Reveal {
            open: root.notes.length > 0
            Notice {
                objectName: "recordingEnding"
                text: root.notes.join(" ")
                tone: root.entry.outcome === "missed" ? "warning" : "info"
                inset: Theme.rowInset
            }
        }
        Reveal {
            open: root.filesStayed.length > 0
            Notice {
                objectName: "recordingFilesStayed"
                text: T.filesStayed(root.filesStayed)
                tone: "negative"
                inset: Theme.rowInset
                actionText: T.text.tryAgain
                onAction: root.remove(true)
                Button { text: T.text.removeOnly; size: "small"; onClicked: root.remove(false) }
            }
        }
        InlineConfirm {
            open: root.askDelete
            text: T.text.deleteRecording
            detail: T.text.deleteRecordingDetail
            confirmText: T.text.delete_
            cancelText: Words.cancel
            leading: CheckBox {
                text: T.text.alsoDeleteFile
                checked: root.alsoDeleteFiles
                onToggled: root.alsoDeleteFiles = checked
            }
            onCancelled: root.askDelete = false
            onConfirmed: {
                root.askDelete = false
                root.remove(root.alsoDeleteFiles)
            }
        }
        Item {
            visible: !root.recording
            width: parent.width
            implicitHeight: refine.implicitHeight > 0 ? refine.implicitHeight + Theme.space3 : 0
            RefineAgain {
                id: refine
                x: Theme.rowInset
                width: parent.width - 2 * Theme.rowInset
                entry: root.entry
            }
        }
        TranscriptReader {
            id: lines
            topPadding: Theme.space3
            bottomPadding: Theme.space3
            model: root.recording ? root.entry.lines || [] : root.paragraphs
            timed: root.recording
            audioEntry: root.recording && root.entry.hasAudio ? root.entryId : 0
            findText: root.find
            maxHeight: 560
        }
    }

    FileDialog {
        id: exportFile
        property string format
        function ask(format) {
            exportFile.format = format
            exportFile.selectedFile = History.exportFile(root.entryId, format)
            exportFile.open()
        }
        title: T.text.exportDialog
        fileMode: FileDialog.SaveFile
        onAccepted: if (History.exportEntry(root.entryId, exportFile.format, exportFile.selectedFile)) Toaster.show(T.text.exported, { icon: "check" })
    }
    // A file's audio found again, which Transcribe again takes.
    FileDialog {
        id: locate
        title: T.text.locateDialog
        fileMode: FileDialog.OpenFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        nameFilters: TranscribeWords.fileFilters(Transcription.extensions)
        onAccepted: root.transcribeAgain(locate.selectedFile)
    }
}
