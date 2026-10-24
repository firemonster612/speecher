pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// Step two: the file being heard, its progress and the queue, beside the
// transcript so far, which follows the newest words.
Row {
    id: view

    readonly property var batchFiles: Transcription.batch.files
    readonly property string phase: TranscribeWords.phases[Transcription.phase]

    width: parent ? parent.width : implicitWidth
    spacing: Theme.space4

    Section {
        id: progress
        width: (parent.width - parent.spacing) / 2
        title: view.phase
        caption: TranscribeWords.processingCount(Math.max(0, Transcription.current), view.batchFiles.length)

        Column {
            x: Theme.rowInset
            width: parent.width - 2 * Theme.rowInset
            topPadding: Theme.space4
            bottomPadding: Theme.space4
            spacing: Theme.space3
            Item {
                width: parent.width
                height: fileName.height
                MorphText {
                    id: fileName
                    width: parent.width - percent.width - Theme.space4
                    text: Transcription.current >= 0 ? view.batchFiles[Transcription.current].name : ""
                    variant: "heading"
                    elide: Text.ElideRight
                }
                MorphText {
                    id: percent
                    anchors.right: parent.right
                    y: fileName.y + (fileName.height - height) / 2
                    text: Words.percent(Transcription.progress)
                    variant: "label"
                    tone: "secondary"
                }
            }
            ProgressWave {
                width: parent.width
                height: 44
                progress: Transcription.progress
                peaks: Transcription.peaks
            }
        }
        RowList {
            id: queue
            values: view.batchFiles
            keyOf: f => f.path
            delegate: ListRow {
                id: queued
                required property string key
                required property int index
                readonly property var file: queue.entry(key)
                readonly property string queueState: Transcription.queue[index] || "waiting"
                width: ListView.view.width
                first: index === 0 && queue.first
                iconName: file.kind === "video" ? "file-video" : "file-audio"
                title: file.name
                detail: TranscribeWords.fileDetail(file)
                trailing: StatusText {
                    text: queued.queueState === "current" ? view.phase : TranscribeWords.queueLabels[queued.queueState]
                    busy: queued.queueState === "current"
                    tone: queued.queueState === "current" ? "accent" : queued.queueState === "failed" ? "negative"
                        : queued.queueState === "done" ? "neutral" : "tertiary"
                    indicator: "none"
                }
            }
        }
    }

    Section {
        width: (parent.width - parent.spacing) / 2
        minimumHeaderHeight: progress.naturalHeaderHeight
        title: TranscribeWords.partialName
        caption: TranscribeWords.partialHelp

        Item {
            width: parent.width
            height: progress.naturalCardHeight
            Flickable {
                id: liveView
                x: Theme.rowInset
                y: Theme.space4
                width: parent.width - 2 * Theme.rowInset
                height: parent.height - 2 * Theme.space4
                contentHeight: live.contentHeight + Theme.space2
                contentY: Math.max(0, contentHeight - height)
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                // A mouse drags nothing on a desktop; touch still does.
                acceptedButtons: Qt.NoButton
                WheelScroll {}
                clip: true
                Behavior on contentY { enabled: Theme.movement > 0; Ease {} }

                Txt {
                    text: TranscribeWords.partialPlaceholder
                    tone: "tertiary"
                    opacity: live.length === 0 ? 1 : 0
                    Behavior on opacity { Ease { duration: Theme.fast } }
                }
                TextEdit {
                    id: live
                    width: liveView.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WordWrap
                    text: Transcription.partial
                    color: Theme.text
                    selectedTextColor: Theme.accentText
                    selectionColor: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTranscript
                    Accessible.name: TranscribeWords.partialName

                    Caret { edit: live }
                }
            }
        }
    }
}
