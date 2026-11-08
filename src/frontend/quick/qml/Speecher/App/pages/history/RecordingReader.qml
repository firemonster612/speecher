pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import "HistoryWords.js" as T

// A Recording opened from History: its name and when it ran, what can be
// done with it (Export, Open file, Show in folder, and Delete, which asks
// first and can take its files too), then its lines. History's search
// carries over into Find, and a matching line it was opened at shows first.
Column {
    id: root

    property real entryId: 0
    // The line to show first, by its time; -1 for the start.
    property real startAt: -1
    property string find
    property bool askDelete: false
    property bool alsoDeleteFiles: false
    // Read again as History changes, so a recording that finishes while
    // open shows all of it.
    readonly property var entry: { void History.kept; return entryId > 0 ? History.entry(entryId) : ({}) }
    signal closed()

    onEntryIdChanged: {
        askDelete = false
        alsoDeleteFiles = !!entry.inRecordingsFolder
        if (startAt >= 0) Qt.callLater(() => lines.showAt(startAt))
    }

    spacing: Theme.space3
    width: parent ? parent.width : implicitWidth

    TextLink { text: T.text.back; iconName: "arrow-left"; onActivated: root.closed() }

    Section {
        id: card
        width: parent.width
        title: root.entryId > 0 ? T.recordingTitle(root.entry) : ""
        caption: root.entryId > 0 ? T.readerDetail(root.entry, Insights.today) : ""
        actions: [
            MenuButton {
                label: T.text.export_
                leadingIcon: "file-down"
                size: "small"
                items: T.text.exportFormats
                onPicked: id => exportFile.ask(id)
            },
            Button { text: T.text.openFile; iconName: "external-link"; size: "small"; enabled: !root.entry.fileMissing; onClicked: History.openFile(root.entryId) },
            Button { text: T.text.showInFolder; iconName: "folder-open"; size: "small"; onClicked: History.showInFolder(root.entryId) },
            Button { objectName: "deleteRecording"; text: T.text.delete_; iconName: "trash-2"; size: "small"; variant: "danger"; onClicked: root.askDelete = true }
        ]
        Reveal {
            open: !!root.entry.fileMissing
            Notice { text: T.text.fileMissing; inset: Theme.rowInset }
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
                if (History.removeRecording(root.entryId, root.alsoDeleteFiles)) root.closed()
            }
        }
        TranscriptReader {
            id: lines
            topPadding: Theme.space3
            bottomPadding: Theme.space3
            model: root.entry.lines || []
            audioEntry: root.entry.hasAudio ? root.entryId : 0
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
}
